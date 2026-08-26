#include "PlaybackStutterProbe.h"

#include "Logger.h"
#include "Settings.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace
{
    constexpr qint64 kWindowMs = 3000;
    constexpr qint64 kLiveRefreshIntervalMs = 120;
    constexpr qint64 kMonitorPollMs = 5;
    constexpr qint64 kUiHitchThresholdMs = 22;
    constexpr qint64 kUiStallThresholdMs = 34;
    constexpr qint64 kUiStallStepMs = 8;
    constexpr qint64 kRecentSampleKeepMs = 6000;
    constexpr qint64 kManualMarkLookbackMs = 450;

    struct DurationBucket
    {
        qint64 count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
        double minMs = std::numeric_limits<double>::max();
        double budgetMs = 0.0;
        qint64 overBudgetCount = 0;
        QVector<double> samples;
    };

    struct SessionState
    {
        bool active = false;
        QString name;
        QString startedAtUtc;
        qint64 startSteadyMs = 0;
        QJsonObject metadata;
        QHash<QString, DurationBucket> durations;
        QHash<QString, qint64> counters;
    };

    struct TimedSample
    {
        qint64 tsMs = 0;
        double valueMs = 0.0;
    };

    struct ProbeState
    {
        qint64 windowStartMs = 0;
        bool lastPlaybackState = false;
        QHash<QString, DurationBucket> durations;
        QHash<QString, qint64> counters;
        QHash<QString, QVector<TimedSample>> recentDurations;
        PlaybackStutterProbe::LiveMetrics live;
        qint64 lastLiveRefreshMs = 0;
        SessionState session;
    };

    qint64 steadyNowMs()
    {
        using namespace std::chrono;
        return static_cast<qint64>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    struct IndependentMonitor
    {
        std::atomic<bool> workerRunning{false};
        std::atomic<bool> stopRequested{false};
        std::atomic<bool> playing{false};
        std::atomic<qint64> lastUiHeartbeatSteadyMs{0};
        std::atomic<qint64> peakUiGapMs{0};
        std::atomic<qint64> hitchEventsPending{0};
        std::atomic<qint64> stallEventsPending{0};
        std::atomic<qint64> generation{0};
        std::thread worker;

        ~IndependentMonitor()
        {
            stopRequested.store(true);
            if (worker.joinable())
                worker.join();
        }
    };

    IndependentMonitor &monitor()
    {
        static IndependentMonitor m;
        return m;
    }

    void ensureMonitorWorkerStarted()
    {
        IndependentMonitor &m = monitor();
        if (m.workerRunning.exchange(true))
            return;

        m.worker = std::thread([]()
                               {
        IndependentMonitor &shared = monitor();
        qint64 lastHitchLevel = -1;
        qint64 lastStallLevel = -1;
        qint64 lastGeneration = -1;
        while (!shared.stopRequested.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kMonitorPollMs));

            if (!shared.playing.load())
            {
                lastHitchLevel = -1;
                lastStallLevel = -1;
                lastGeneration = shared.generation.load();
                continue;
            }

            const qint64 currentGen = shared.generation.load();
            if (currentGen != lastGeneration)
            {
                lastGeneration = currentGen;
                lastHitchLevel = -1;
                lastStallLevel = -1;
            }

            const qint64 hb = shared.lastUiHeartbeatSteadyMs.load();
            if (hb <= 0)
                continue;

            const qint64 gapMs = steadyNowMs() - hb;
            if (gapMs <= 0)
                continue;

            qint64 peak = shared.peakUiGapMs.load();
            while (gapMs > peak && !shared.peakUiGapMs.compare_exchange_weak(peak, gapMs))
            {
            }

            if (gapMs >= kUiHitchThresholdMs)
            {
                const qint64 hitchLevel = gapMs / 4;
                if (hitchLevel != lastHitchLevel)
                {
                    shared.hitchEventsPending.fetch_add(1);
                    lastHitchLevel = hitchLevel;
                }
            }
            else
            {
                lastHitchLevel = -1;
            }

            if (gapMs >= kUiStallThresholdMs)
            {
                const qint64 stallLevel = gapMs / kUiStallStepMs;
                if (stallLevel != lastStallLevel)
                {
                    shared.stallEventsPending.fetch_add(1);
                    lastStallLevel = stallLevel;
                }
            }
            else
            {
                lastStallLevel = -1;
            }
        } });
    }

    ProbeState &state()
    {
        static ProbeState s;
        return s;
    }

    std::atomic<qint64> &manualMarkSeq()
    {
        static std::atomic<qint64> seq{0};
        return seq;
    }

    std::atomic<bool> &processOverrideEnabled()
    {
        static std::atomic<bool> value{false};
        return value;
    }

    bool probeEnabled()
    {
        if (processOverrideEnabled().load(std::memory_order_relaxed))
            return true;

        static std::atomic<int> cached{-1};
        static std::atomic<qint64> lastRefreshMs{0};
        const qint64 tsMs = QDateTime::currentMSecsSinceEpoch();
        const int last = cached.load(std::memory_order_relaxed);
        const qint64 refreshAt = lastRefreshMs.load(std::memory_order_relaxed);
        if (last >= 0 && (tsMs - refreshAt) < 250)
            return last == 1;

        const bool enabled = Settings::instance().playbackStutterProbeEnabled();
        cached.store(enabled ? 1 : 0, std::memory_order_relaxed);
        lastRefreshMs.store(tsMs, std::memory_order_relaxed);
        return enabled;
    }

    qint64 nowMs()
    {
        return QDateTime::currentMSecsSinceEpoch();
    }

    void resetWindow(ProbeState &s, qint64 tsMs)
    {
        s.windowStartMs = tsMs;
        s.durations.clear();
        s.counters.clear();
    }

    void clearState(ProbeState &s)
    {
        resetWindow(s, 0);
        s.lastPlaybackState = false;
        s.recentDurations.clear();
        s.live = PlaybackStutterProbe::LiveMetrics{};
        s.lastLiveRefreshMs = 0;
        s.session = SessionState{};
    }

    void pruneRecentSamples(QVector<TimedSample> *samples, qint64 minTsMs)
    {
        if (!samples)
            return;
        while (!samples->isEmpty() && samples->front().tsMs < minTsMs)
            samples->removeFirst();
    }

    double percentile(const QVector<double> &sortedSamples, double p)
    {
        if (sortedSamples.isEmpty())
            return 0.0;
        const double clamped = std::clamp(p, 0.0, 1.0);
        const int idx = static_cast<int>(clamped * static_cast<double>(sortedSamples.size() - 1));
        return sortedSamples[idx];
    }

    QString bucketSummary(const QString &key, DurationBucket bucket)
    {
        if (bucket.count <= 0)
            return QString();

        std::sort(bucket.samples.begin(), bucket.samples.end());
        const double avg = bucket.totalMs / static_cast<double>(bucket.count);
        const double p95 = percentile(bucket.samples, 0.95);
        const double slowRatio = bucket.count > 0
                                     ? (static_cast<double>(bucket.overBudgetCount) * 100.0 / static_cast<double>(bucket.count))
                                     : 0.0;
        return QString("%1(avg=%2ms,p95=%3ms,max=%4ms,n=%5,slow=%6%%)")
            .arg(key)
            .arg(avg, 0, 'f', 2)
            .arg(p95, 0, 'f', 2)
            .arg(bucket.maxMs, 0, 'f', 2)
            .arg(bucket.count)
            .arg(slowRatio, 0, 'f', 1);
    }

    double bucketP95(DurationBucket bucket)
    {
        if (bucket.samples.isEmpty())
            return 0.0;
        std::sort(bucket.samples.begin(), bucket.samples.end());
        return percentile(bucket.samples, 0.95);
    }

    double bucketSlowRatio(DurationBucket bucket)
    {
        if (bucket.count <= 0)
            return 0.0;
        return static_cast<double>(bucket.overBudgetCount) * 100.0 / static_cast<double>(bucket.count);
    }

    double bucketStdDev(DurationBucket bucket)
    {
        if (bucket.samples.size() <= 1)
            return 0.0;
        double sum = 0.0;
        for (double v : bucket.samples)
            sum += v;
        const double mean = sum / static_cast<double>(bucket.samples.size());
        double var = 0.0;
        for (double v : bucket.samples)
        {
            const double d = v - mean;
            var += d * d;
        }
        var /= static_cast<double>(bucket.samples.size());
        return std::sqrt(var);
    }

    void addDurationSample(DurationBucket &bucket, double elapsedMs, double budgetMs)
    {
        bucket.count += 1;
        bucket.totalMs += elapsedMs;
        bucket.maxMs = qMax(bucket.maxMs, elapsedMs);
        bucket.minMs = qMin(bucket.minMs, elapsedMs);
        bucket.budgetMs = budgetMs;
        if (elapsedMs > budgetMs)
            bucket.overBudgetCount += 1;
        bucket.samples.append(elapsedMs);
    }

    QJsonObject durationBucketJson(DurationBucket bucket)
    {
        QJsonObject result;
        if (bucket.count <= 0)
            return result;

        std::sort(bucket.samples.begin(), bucket.samples.end());
        result.insert("count", bucket.count);
        result.insert("average_ms", bucket.totalMs / static_cast<double>(bucket.count));
        result.insert("minimum_ms",
                      bucket.minMs == std::numeric_limits<double>::max() ? 0.0 : bucket.minMs);
        result.insert("p50_ms", percentile(bucket.samples, 0.50));
        result.insert("p95_ms", percentile(bucket.samples, 0.95));
        result.insert("p99_ms", percentile(bucket.samples, 0.99));
        result.insert("maximum_ms", bucket.maxMs);
        result.insert("standard_deviation_ms", bucketStdDev(bucket));
        result.insert("budget_ms", bucket.budgetMs);
        result.insert("over_budget_count", bucket.overBudgetCount);
        result.insert("over_budget_percent",
                      static_cast<double>(bucket.overBudgetCount) * 100.0 /
                          static_cast<double>(bucket.count));
        return result;
    }

    double recentMaxMs(const ProbeState &s, const QString &key, qint64 sinceTsMs)
    {
        const auto it = s.recentDurations.constFind(key);
        if (it == s.recentDurations.constEnd() || it->isEmpty())
            return 0.0;

        bool hasValue = false;
        double maxValue = 0.0;
        for (const TimedSample &sample : it.value())
        {
            if (sample.tsMs < sinceTsMs)
                continue;
            if (!hasValue)
            {
                maxValue = sample.valueMs;
                hasValue = true;
                continue;
            }
            maxValue = qMax(maxValue, sample.valueMs);
        }
        return hasValue ? maxValue : 0.0;
    }

    double recentP95Ms(const ProbeState &s, const QString &key, qint64 sinceTsMs)
    {
        const auto it = s.recentDurations.constFind(key);
        if (it == s.recentDurations.constEnd() || it->isEmpty())
            return 0.0;

        QVector<double> recentValues;
        recentValues.reserve(it->size());
        for (const TimedSample &sample : it.value())
        {
            if (sample.tsMs >= sinceTsMs)
                recentValues.append(sample.valueMs);
        }
        if (recentValues.isEmpty())
            return 0.0;
        std::sort(recentValues.begin(), recentValues.end());
        return percentile(recentValues, 0.95);
    }

    struct DerivedWindowMetrics
    {
        QStringList topList;
        QStringList counters;
        double fpsTick = 0.0;
        double fpsCanvas = 0.0;
        double fpsPreview = 0.0;
        double jitterP95Ms = 0.0;
        double pacingStdMs = 0.0;
        double pacingJerkP95Ms = 0.0;
        double stepJerkP95Ms = 0.0;
        double uiGapP95Ms = 0.0;
        double visualScrollStepP95Px = 0.0;
        double visualScrollJerkP95Px = 0.0;
        double visualPlayheadDriftP95Px = 0.0;
        double visualPlayheadStepJerkP95Px = 0.0;
        double jitterSlowPct = 0.0;
        double canvasSlowPct = 0.0;
        qint64 jankEvents = 0;
        qint64 stepJankEvents = 0;
        qint64 visualJankEvents = 0;
        qint64 visualBacktrackEvents = 0;
        qint64 manualJerkMarks = 0;
        qint64 uiHitchEvents = 0;
        qint64 uiStallEvents = 0;
    };

    DerivedWindowMetrics deriveWindowMetrics(const ProbeState &s, qint64 elapsedMs)
    {
        DerivedWindowMetrics out;

        struct RankedEntry
        {
            QString key;
            DurationBucket bucket;
        };

        QVector<RankedEntry> ranked;
        ranked.reserve(s.durations.size());
        for (auto it = s.durations.constBegin(); it != s.durations.constEnd(); ++it)
            ranked.push_back({it.key(), it.value()});

        std::sort(ranked.begin(), ranked.end(), [](const RankedEntry &a, const RankedEntry &b)
                  { return a.bucket.totalMs > b.bucket.totalMs; });

        const int topN = qMin(3, ranked.size());
        for (int i = 0; i < topN; ++i)
        {
            const QString summary = bucketSummary(ranked[i].key, ranked[i].bucket);
            if (!summary.isEmpty())
                out.topList.append(summary);
        }

        for (auto it = s.counters.constBegin(); it != s.counters.constEnd(); ++it)
            out.counters.append(QString("%1=%2").arg(it.key()).arg(it.value()));
        std::sort(out.counters.begin(), out.counters.end());

        const auto tickIt = s.durations.constFind("playback.pulse_interval");
        const auto jerkIt = s.durations.constFind("playback.pulse_interval_jerk");
        const auto stepJerkIt = s.durations.constFind("playback.time_step_jerk");
        const auto uiGapIt = s.durations.constFind("monitor.ui_heartbeat_gap");
        const auto visualScrollStepIt = s.durations.constFind("visual.scroll_step_px");
        const auto visualScrollJerkIt = s.durations.constFind("visual.scroll_step_jerk_px");
        const auto visualPlayheadDriftIt = s.durations.constFind("visual.playhead_drift_px");
        const auto visualPlayheadStepJerkIt = s.durations.constFind("visual.playhead_step_jerk_px");
        const auto canvasIt = s.durations.constFind("canvas.paint_total");
        const auto previewIt = s.durations.constFind("preview.paint_total");

        out.fpsTick = (tickIt != s.durations.constEnd() && elapsedMs > 0)
                          ? (static_cast<double>(tickIt->count) * 1000.0 / static_cast<double>(elapsedMs))
                          : 0.0;
        out.fpsCanvas = (canvasIt != s.durations.constEnd() && elapsedMs > 0)
                            ? (static_cast<double>(canvasIt->count) * 1000.0 / static_cast<double>(elapsedMs))
                            : 0.0;
        out.fpsPreview = (previewIt != s.durations.constEnd() && elapsedMs > 0)
                             ? (static_cast<double>(previewIt->count) * 1000.0 / static_cast<double>(elapsedMs))
                             : 0.0;
        out.jitterP95Ms = (tickIt != s.durations.constEnd()) ? bucketP95(tickIt.value()) : 0.0;
        out.pacingStdMs = (tickIt != s.durations.constEnd()) ? bucketStdDev(tickIt.value()) : 0.0;
        out.pacingJerkP95Ms = (jerkIt != s.durations.constEnd()) ? bucketP95(jerkIt.value()) : 0.0;
        out.stepJerkP95Ms = (stepJerkIt != s.durations.constEnd()) ? bucketP95(stepJerkIt.value()) : 0.0;
        out.uiGapP95Ms = (uiGapIt != s.durations.constEnd()) ? bucketP95(uiGapIt.value()) : 0.0;
        out.visualScrollStepP95Px = (visualScrollStepIt != s.durations.constEnd()) ? bucketP95(visualScrollStepIt.value()) : 0.0;
        out.visualScrollJerkP95Px = (visualScrollJerkIt != s.durations.constEnd()) ? bucketP95(visualScrollJerkIt.value()) : 0.0;
        out.visualPlayheadDriftP95Px = (visualPlayheadDriftIt != s.durations.constEnd()) ? bucketP95(visualPlayheadDriftIt.value()) : 0.0;
        out.visualPlayheadStepJerkP95Px = (visualPlayheadStepJerkIt != s.durations.constEnd()) ? bucketP95(visualPlayheadStepJerkIt.value()) : 0.0;
        out.jitterSlowPct = (tickIt != s.durations.constEnd()) ? bucketSlowRatio(tickIt.value()) : 0.0;
        out.canvasSlowPct = (canvasIt != s.durations.constEnd()) ? bucketSlowRatio(canvasIt.value()) : 0.0;
        out.jankEvents = s.counters.value("playback.pulse_jank_events", 0);
        out.stepJankEvents = s.counters.value("playback.time_step_jank_events", 0);
        out.visualJankEvents = s.counters.value("visual.scroll_step_jank_events", 0) +
                               s.counters.value("visual.playhead_step_jank_events", 0) +
                               s.counters.value("visual.playhead_drift_events", 0);
        out.visualBacktrackEvents = s.counters.value("visual.scroll_backtrack_events", 0);
        out.manualJerkMarks = s.counters.value("manual.jerk_marks", 0);
        out.uiHitchEvents = s.counters.value("monitor.ui_hitch_events", 0);
        out.uiStallEvents = s.counters.value("monitor.ui_stall_events", 0);
        return out;
    }

    void refreshLiveMetrics(ProbeState &s, qint64 elapsedMs)
    {
        if (elapsedMs <= 0)
            return;
        const DerivedWindowMetrics m = deriveWindowMetrics(s, elapsedMs);
        s.live.valid = true;
        s.live.windowMs = elapsedMs;
        s.live.fpsTick = m.fpsTick;
        s.live.fpsCanvas = m.fpsCanvas;
        s.live.fpsPreview = m.fpsPreview;
        s.live.jitterP95Ms = m.jitterP95Ms;
        s.live.pacingStdMs = m.pacingStdMs;
        s.live.pacingJerkP95Ms = m.pacingJerkP95Ms;
        s.live.stepJerkP95Ms = m.stepJerkP95Ms;
        s.live.uiGapP95Ms = m.uiGapP95Ms;
        s.live.visualScrollStepP95Px = m.visualScrollStepP95Px;
        s.live.visualScrollJerkP95Px = m.visualScrollJerkP95Px;
        s.live.visualPlayheadDriftP95Px = m.visualPlayheadDriftP95Px;
        s.live.visualPlayheadStepJerkP95Px = m.visualPlayheadStepJerkP95Px;
        s.live.jitterSlowPct = m.jitterSlowPct;
        s.live.canvasSlowPct = m.canvasSlowPct;
        s.live.jankEvents = m.jankEvents;
        s.live.stepJankEvents = m.stepJankEvents;
        s.live.visualJankEvents = m.visualJankEvents;
        s.live.visualBacktrackEvents = m.visualBacktrackEvents;
        s.live.manualJerkMarks = m.manualJerkMarks;
        s.live.uiHitchEvents = m.uiHitchEvents;
        s.live.uiStallEvents = m.uiStallEvents;
        s.live.topHotspot = m.topList.isEmpty() ? QString() : m.topList.first();
        s.live.lastUpdateMs = nowMs();
        s.lastLiveRefreshMs = s.live.lastUpdateMs;
    }

    void recordIndependentMonitorSamples(bool playing)
    {
        if (!playing)
            return;

        IndependentMonitor &m = monitor();
        const qint64 peakGapMs = m.peakUiGapMs.exchange(0);
        if (peakGapMs > 0)
            PlaybackStutterProbe::recordDuration("monitor.ui_heartbeat_gap", static_cast<double>(peakGapMs), 24.0, true);

        const qint64 hitchEvents = m.hitchEventsPending.exchange(0);
        if (hitchEvents > 0)
            PlaybackStutterProbe::recordCounter("monitor.ui_hitch_events", hitchEvents, true);

        const qint64 stallEvents = m.stallEventsPending.exchange(0);
        if (stallEvents > 0)
            PlaybackStutterProbe::recordCounter("monitor.ui_stall_events", stallEvents, true);
    }

    void flushIfNeeded(bool force)
    {
        ProbeState &s = state();
        const qint64 tsMs = nowMs();
        if (s.windowStartMs == 0)
            s.windowStartMs = tsMs;

        const qint64 elapsedMs = tsMs - s.windowStartMs;
        if (!force && elapsedMs < kWindowMs)
            return;

        if (s.durations.isEmpty() && s.counters.isEmpty())
        {
            resetWindow(s, tsMs);
            return;
        }

        const DerivedWindowMetrics metrics = deriveWindowMetrics(s, elapsedMs);
        refreshLiveMetrics(s, elapsedMs);

        Logger::info(QString("PERF_PLAYBACK window_ms=%1 fps_tick=%2 fps_canvas=%3 fps_preview=%4 jitter_p95_ms=%5 pacing_std_ms=%6 pacing_jerk_p95_ms=%7 step_jerk_p95_ms=%8 ui_gap_p95_ms=%9 jank_events=%10 step_jank_events=%11 manual_jerk_marks=%12 ui_hitch_events=%13 ui_stall_events=%14 jitter_slow_pct=%15 canvas_slow_pct=%16 top=[%17] counters=[%18]")
                         .arg(elapsedMs)
                         .arg(metrics.fpsTick, 0, 'f', 1)
                         .arg(metrics.fpsCanvas, 0, 'f', 1)
                         .arg(metrics.fpsPreview, 0, 'f', 1)
                         .arg(metrics.jitterP95Ms, 0, 'f', 2)
                         .arg(metrics.pacingStdMs, 0, 'f', 2)
                         .arg(metrics.pacingJerkP95Ms, 0, 'f', 2)
                         .arg(metrics.stepJerkP95Ms, 0, 'f', 2)
                         .arg(metrics.uiGapP95Ms, 0, 'f', 2)
                         .arg(metrics.jankEvents)
                         .arg(metrics.stepJankEvents)
                         .arg(metrics.manualJerkMarks)
                         .arg(metrics.uiHitchEvents)
                         .arg(metrics.uiStallEvents)
                         .arg(metrics.jitterSlowPct, 0, 'f', 1)
                         .arg(metrics.canvasSlowPct, 0, 'f', 1)
                         .arg(metrics.topList.join("; "))
                         .arg(metrics.counters.join(", ")));

        if (Logger::isJsonLoggingEnabled())
        {
            QMap<QString, QString> context;
            context.insert("window_ms", QString::number(elapsedMs));
            context.insert("fps_tick", QString::number(metrics.fpsTick, 'f', 1));
            context.insert("fps_canvas", QString::number(metrics.fpsCanvas, 'f', 1));
            context.insert("fps_preview", QString::number(metrics.fpsPreview, 'f', 1));
            context.insert("jitter_p95_ms", QString::number(metrics.jitterP95Ms, 'f', 2));
            context.insert("pacing_std_ms", QString::number(metrics.pacingStdMs, 'f', 2));
            context.insert("pacing_jerk_p95_ms", QString::number(metrics.pacingJerkP95Ms, 'f', 2));
            context.insert("step_jerk_p95_ms", QString::number(metrics.stepJerkP95Ms, 'f', 2));
            context.insert("ui_gap_p95_ms", QString::number(metrics.uiGapP95Ms, 'f', 2));
            context.insert("jank_events", QString::number(metrics.jankEvents));
            context.insert("step_jank_events", QString::number(metrics.stepJankEvents));
            context.insert("manual_jerk_marks", QString::number(metrics.manualJerkMarks));
            context.insert("ui_hitch_events", QString::number(metrics.uiHitchEvents));
            context.insert("ui_stall_events", QString::number(metrics.uiStallEvents));
            context.insert("jitter_slow_pct", QString::number(metrics.jitterSlowPct, 'f', 1));
            context.insert("canvas_slow_pct", QString::number(metrics.canvasSlowPct, 'f', 1));
            context.insert("top", metrics.topList.join("; "));
            context.insert("counters", metrics.counters.join(", "));
            Logger::logStructured(Logger::DEBUG, "PERF_PLAYBACK_WINDOW", "PlaybackStutterProbe", context);
        }

        resetWindow(s, tsMs);
    }
} // namespace

namespace PlaybackStutterProbe
{
    bool enabled()
    {
        return probeEnabled();
    }

    void setProcessOverrideEnabled(bool forceEnabled)
    {
        processOverrideEnabled().store(forceEnabled, std::memory_order_relaxed);
        if (!PlaybackStutterProbe::enabled() && !forceEnabled)
        {
            ProbeState &s = state();
            clearState(s);
            monitor().playing.store(false);
        }
    }

    void recordDuration(const QString &key, double elapsedMs, double budgetMs, bool playing)
    {
        if (!enabled())
        {
            ProbeState &s = state();
            clearState(s);
            monitor().playing.store(false);
            return;
        }
        if (!playing || elapsedMs < 0.0)
            return;

        ProbeState &s = state();
        const qint64 tsMs = nowMs();
        if (s.windowStartMs == 0)
            s.windowStartMs = tsMs;

        addDurationSample(s.durations[key], elapsedMs, budgetMs);
        if (s.session.active)
            addDurationSample(s.session.durations[key], elapsedMs, budgetMs);

        QVector<TimedSample> &recent = s.recentDurations[key];
        recent.append(TimedSample{tsMs, elapsedMs});
        pruneRecentSamples(&recent, tsMs - kRecentSampleKeepMs);

        const qint64 elapsedWindowMs = tsMs - s.windowStartMs;
        if (s.lastLiveRefreshMs == 0 || tsMs - s.lastLiveRefreshMs >= kLiveRefreshIntervalMs)
            refreshLiveMetrics(s, elapsedWindowMs);
        flushIfNeeded(false);
    }

    void recordCounter(const QString &key, qint64 delta, bool playing)
    {
        if (!enabled())
        {
            ProbeState &s = state();
            clearState(s);
            monitor().playing.store(false);
            return;
        }
        if (!playing || delta == 0)
            return;

        ProbeState &s = state();
        if (s.windowStartMs == 0)
            s.windowStartMs = nowMs();

        s.counters[key] += delta;
        if (s.session.active)
            s.session.counters[key] += delta;
        const qint64 tsMs = nowMs();
        const qint64 elapsedWindowMs = tsMs - s.windowStartMs;
        if (s.lastLiveRefreshMs == 0 || tsMs - s.lastLiveRefreshMs >= kLiveRefreshIntervalMs)
            refreshLiveMetrics(s, elapsedWindowMs);
        flushIfNeeded(false);
    }

    void markPlaybackState(bool playing)
    {
        if (!enabled())
        {
            ProbeState &s = state();
            clearState(s);
            monitor().playing.store(false);
            return;
        }

        ensureMonitorWorkerStarted();
        IndependentMonitor &m = monitor();
        ProbeState &s = state();
        if (s.lastPlaybackState && !playing)
        {
            recordIndependentMonitorSamples(true);
            flushIfNeeded(true);
        }
        s.lastPlaybackState = playing;
        m.playing.store(playing);
        if (playing)
        {
            m.generation.fetch_add(1);
            m.lastUiHeartbeatSteadyMs.store(steadyNowMs());
            m.peakUiGapMs.store(0);
            m.hitchEventsPending.store(0);
            m.stallEventsPending.store(0);
        }
    }

    void markUiHeartbeat(bool playing)
    {
        if (!playing || !enabled())
            return;

        ensureMonitorWorkerStarted();
        IndependentMonitor &m = monitor();
        static std::atomic<qint64> lastDrainSteadyMs{0};
        const qint64 steadyMs = steadyNowMs();
        m.lastUiHeartbeatSteadyMs.store(steadyMs);
        m.playing.store(true);
        qint64 expectedDrain = lastDrainSteadyMs.load(std::memory_order_relaxed);
        if (expectedDrain == 0 || steadyMs - expectedDrain >= 32)
        {
            if (lastDrainSteadyMs.compare_exchange_strong(expectedDrain, steadyMs, std::memory_order_relaxed))
                recordIndependentMonitorSamples(true);
        }
    }

    void markManualJerk(double playbackTimeMs, qint64 frameSeq)
    {
        if (!enabled())
            return;

        ProbeState &s = state();
        const qint64 markTsMs = nowMs();
        const qint64 markId = manualMarkSeq().fetch_add(1) + 1;

        // Treat manual marks as high-priority evidence and keep them in counters even if
        // the mark is pressed slightly after the perceived jerk.
        PlaybackStutterProbe::recordCounter("manual.jerk_marks", 1, true);

        Logger::info(QString("PERF_PLAYBACK_MARK type=manual_jerk mark_id=%1 play_time_ms=%2 frame_seq=%3")
                         .arg(markId)
                         .arg(playbackTimeMs, 0, 'f', 2)
                         .arg(frameSeq));

        const qint64 sinceTsMs = markTsMs - kManualMarkLookbackMs;
        const double pulseMaxMs = recentMaxMs(s, "playback.pulse_interval", sinceTsMs);
        const double pulseP95Ms = recentP95Ms(s, "playback.pulse_interval", sinceTsMs);
        const double stepMaxMs = recentMaxMs(s, "playback.time_step_ms", sinceTsMs);
        const double stepP95Ms = recentP95Ms(s, "playback.time_step_ms", sinceTsMs);
        const double uiGapMaxMs = recentMaxMs(s, "monitor.ui_heartbeat_gap", sinceTsMs);
        const double visualScrollStepMaxPx = recentMaxMs(s, "visual.scroll_step_px", sinceTsMs);
        const double visualScrollJerkMaxPx = recentMaxMs(s, "visual.scroll_step_jerk_px", sinceTsMs);
        const double visualPlayheadDriftMaxPx = recentMaxMs(s, "visual.playhead_drift_px", sinceTsMs);
        const double canvasMaxMs = recentMaxMs(s, "canvas.paint_total", sinceTsMs);
        const double previewMaxMs = recentMaxMs(s, "preview.paint_total", sinceTsMs);
        const PlaybackStutterProbe::LiveMetrics live = latestMetrics();

        Logger::info(QString("PERF_PLAYBACK_MARK_CONTEXT type=manual_jerk mark_id=%1 lookback_ms=%2 pulse_max_ms=%3 pulse_p95_ms=%4 step_max_ms=%5 step_p95_ms=%6 ui_gap_max_ms=%7 canvas_max_ms=%8 preview_max_ms=%9 live_jitter_p95_ms=%10 live_step_jerk_p95_ms=%11 live_ui_gap_p95_ms=%12 visual_scroll_step_max_px=%13 visual_scroll_jerk_max_px=%14 visual_playhead_drift_max_px=%15 live_visual_scroll_jerk_p95_px=%16 live_visual_playhead_drift_p95_px=%17")
                         .arg(markId)
                         .arg(kManualMarkLookbackMs)
                         .arg(pulseMaxMs, 0, 'f', 2)
                         .arg(pulseP95Ms, 0, 'f', 2)
                         .arg(stepMaxMs, 0, 'f', 2)
                         .arg(stepP95Ms, 0, 'f', 2)
                         .arg(uiGapMaxMs, 0, 'f', 2)
                         .arg(canvasMaxMs, 0, 'f', 2)
                         .arg(previewMaxMs, 0, 'f', 2)
                         .arg(live.jitterP95Ms, 0, 'f', 2)
                         .arg(live.stepJerkP95Ms, 0, 'f', 2)
                         .arg(live.uiGapP95Ms, 0, 'f', 2)
                         .arg(visualScrollStepMaxPx, 0, 'f', 2)
                         .arg(visualScrollJerkMaxPx, 0, 'f', 2)
                         .arg(visualPlayheadDriftMaxPx, 0, 'f', 2)
                         .arg(live.visualScrollJerkP95Px, 0, 'f', 2)
                         .arg(live.visualPlayheadDriftP95Px, 0, 'f', 2));
    }

    void forceFlush()
    {
        if (!enabled())
            return;
        flushIfNeeded(true);
    }

    LiveMetrics latestMetrics()
    {
        const ProbeState &s = state();
        return s.live;
    }

    void beginSession(const QString &name, const QJsonObject &metadata)
    {
        ProbeState &s = state();
        s.session = SessionState{};
        s.session.active = true;
        s.session.name = name.trimmed().isEmpty() ? QStringLiteral("playback") : name.trimmed();
        s.session.startedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        s.session.startSteadyMs = steadyNowMs();
        s.session.metadata = metadata;
    }

    QJsonObject endSession()
    {
        ProbeState &s = state();
        if (!s.session.active)
            return QJsonObject{{"error", QStringLiteral("No active performance session.")}};

        const qint64 elapsedMs = qMax<qint64>(1, steadyNowMs() - s.session.startSteadyMs);
        QJsonObject durationResults;
        QStringList durationKeys = s.session.durations.keys();
        std::sort(durationKeys.begin(), durationKeys.end());
        for (const QString &key : durationKeys)
            durationResults.insert(key, durationBucketJson(s.session.durations.value(key)));

        QJsonObject counterResults;
        QStringList counterKeys = s.session.counters.keys();
        std::sort(counterKeys.begin(), counterKeys.end());
        for (const QString &key : counterKeys)
            counterResults.insert(key, static_cast<double>(s.session.counters.value(key)));

        const auto sampleCount = [&s](const QString &key) -> qint64
        {
            const auto it = s.session.durations.constFind(key);
            return it == s.session.durations.constEnd() ? 0 : it->count;
        };
        const auto samplesPerSecond = [elapsedMs, &sampleCount](const QString &key) -> double
        {
            return static_cast<double>(sampleCount(key)) * 1000.0 / static_cast<double>(elapsedMs);
        };
        const auto p95 = [&s](const QString &key) -> double
        {
            const auto it = s.session.durations.constFind(key);
            return it == s.session.durations.constEnd() ? 0.0 : bucketP95(it.value());
        };

        QJsonObject summary;
        summary.insert("fps_tick", samplesPerSecond(QStringLiteral("playback.pulse_interval")));
        summary.insert("fps_canvas", samplesPerSecond(QStringLiteral("canvas.paint_total")));
        summary.insert("fps_preview", samplesPerSecond(QStringLiteral("preview.paint_total")));
        summary.insert("pulse_interval_p95_ms", p95(QStringLiteral("playback.pulse_interval")));
        summary.insert("paint_interval_p95_ms", p95(QStringLiteral("canvas.paint_interval")));
        summary.insert("paint_time_p95_ms", p95(QStringLiteral("canvas.paint_total")));
        summary.insert("ui_gap_p95_ms", p95(QStringLiteral("monitor.ui_heartbeat_gap")));
        summary.insert("pulse_over_budget_count",
                       static_cast<double>(s.session.durations.value(QStringLiteral("playback.pulse_interval")).overBudgetCount));
        summary.insert("paint_over_budget_count",
                       static_cast<double>(s.session.durations.value(QStringLiteral("canvas.paint_total")).overBudgetCount));
        summary.insert("ui_hitch_events",
                       static_cast<double>(s.session.counters.value(QStringLiteral("monitor.ui_hitch_events"))));
        summary.insert("ui_stall_events",
                       static_cast<double>(s.session.counters.value(QStringLiteral("monitor.ui_stall_events"))));

        QJsonObject verdict;
        const double targetFps = s.session.metadata.value(QStringLiteral("effective_fps")).toDouble();
        if (targetFps > 0.0)
        {
            const double frameBudgetMs = 1000.0 / targetFps;
            const double canvasFps = summary.value(QStringLiteral("fps_canvas")).toDouble();
            const double pulseP95Ms = summary.value(QStringLiteral("pulse_interval_p95_ms")).toDouble();
            const double paintP95Ms = summary.value(QStringLiteral("paint_time_p95_ms")).toDouble();
            const qint64 uiStalls = s.session.counters.value(QStringLiteral("monitor.ui_stall_events"));
            const bool fpsPass = canvasFps >= targetFps * 0.95;
            const bool cadencePass = pulseP95Ms > 0.0 && pulseP95Ms <= frameBudgetMs * 1.35;
            const bool paintPass = paintP95Ms > 0.0 && paintP95Ms <= frameBudgetMs;
            const bool stallsPass = uiStalls == 0;

            QJsonObject criteria;
            criteria.insert("canvas_fps_at_least_95_percent", fpsPass);
            criteria.insert("pulse_p95_within_135_percent_budget", cadencePass);
            criteria.insert("paint_p95_within_frame_budget", paintPass);
            criteria.insert("no_ui_stalls", stallsPass);

            QJsonArray failures;
            if (!fpsPass)
                failures.append(QStringLiteral("canvas_fps_below_target"));
            if (!cadencePass)
                failures.append(QStringLiteral("pulse_cadence_over_budget"));
            if (!paintPass)
                failures.append(QStringLiteral("paint_time_over_budget"));
            if (!stallsPass)
                failures.append(QStringLiteral("ui_stalls_detected"));

            verdict.insert("passed", failures.isEmpty());
            verdict.insert("target_fps", targetFps);
            verdict.insert("frame_budget_ms", frameBudgetMs);
            verdict.insert("fps_achievement_percent", canvasFps * 100.0 / targetFps);
            verdict.insert("criteria", criteria);
            verdict.insert("failures", failures);
        }

        QJsonObject report;
        report.insert("schema_version", 1);
        report.insert("session", s.session.name);
        report.insert("started_at_utc", s.session.startedAtUtc);
        report.insert("finished_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        report.insert("duration_ms", static_cast<double>(elapsedMs));
        report.insert("metadata", s.session.metadata);
        report.insert("summary", summary);
        report.insert("verdict", verdict);
        report.insert("durations", durationResults);
        report.insert("counters", counterResults);

        s.session.active = false;
        return report;
    }

    bool sessionActive()
    {
        return state().session.active;
    }
} // namespace PlaybackStutterProbe

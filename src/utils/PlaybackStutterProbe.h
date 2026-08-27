#pragma once

#include <QJsonObject>
#include <QString>

namespace PlaybackStutterProbe
{
    bool enabled();
    void setProcessOverrideEnabled(bool enabled);

    struct LiveMetrics
    {
        bool valid = false;
        qint64 windowMs = 0;
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
        double visualScrollVelocityChangeP95Pct = 0.0;
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
        QString topHotspot;
        qint64 lastUpdateMs = 0;
    };

    void recordDuration(const QString &key, double elapsedMs, double budgetMs, bool playing);
    void recordValue(const QString &key, double value, double budget, bool playing);
    void recordCounter(const QString &key, qint64 delta, bool playing);
    void markPlaybackState(bool playing);
    void markUiHeartbeat(bool playing);
    void markManualJerk(double playbackTimeMs, qint64 frameSeq);
    void forceFlush();
    LiveMetrics latestMetrics();
    void beginSession(const QString &name, const QJsonObject &metadata = QJsonObject());
    QJsonObject endSession();
    bool sessionActive();
}

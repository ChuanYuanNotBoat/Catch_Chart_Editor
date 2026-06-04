#pragma once

#include <QSet>
#include <QVector>
#include <QWidget>
#include <QElapsedTimer>
#include "utils/MathUtils.h"

class ChartController;
class PlaybackController;
class Skin;
class NoteRenderer;
class HyperfruitDetector;
class QTimer;

class RealtimePreviewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RealtimePreviewWidget(QWidget *parent = nullptr);
    ~RealtimePreviewWidget() override;

    void setChartController(ChartController *controller);
    void setPlaybackController(PlaybackController *controller);
    void setSkin(Skin *skin);
    void setColorMode(bool enabled);
    void setHyperfruitEnabled(bool enabled);
    void setNoteSize(int size);

public slots:
    void setCurrentBeat(double beat);
    void setCurrentTimeMs(double timeMs);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int kLaneWidth = 512;
    static constexpr int kMinFrameIntervalMs = 16;

    // 使用 MathUtils::BpmCacheEntry 作为 BPM 段数据，消除重复结构体定义
    using BpmSegment = MathUtils::BpmCacheEntry;

    struct TimedNoteEntry
    {
        int index = -1;
        double startMs = 0.0;
        double endMs = 0.0;
    };

    double beatToTimeMs(double beat);
    double timeToY(double timeMs,
                   const QRectF &laneRect,
                   double referenceY,
                   double upperSpanMs,
                   double lowerSpanMs) const;
    void scheduleUpdate();
    void invalidateNoteCache();
    void ensureNoteCache();
    void invalidateHyperCache();
    void ensureHyperCache();
    void onPlaybackStateChanged();
    void handlePlaybackFrameTick(double predictedTimeMs, qint64 frameSeq);

    ChartController *m_chartController = nullptr;
    PlaybackController *m_playbackController = nullptr;
    NoteRenderer *m_noteRenderer = nullptr;
    HyperfruitDetector *m_hyperfruitDetector = nullptr;
    bool m_colorMode = true;
    bool m_hyperfruitEnabled = true;
    double m_currentTimeMs = 0.0;
    QVector<BpmSegment> m_bpmSegments;
    QVector<double> m_noteStartTimesMs;
    QVector<double> m_noteEndTimesMs;
    QVector<int> m_normalIndices;
    QVector<int> m_rainIndices;
    QVector<TimedNoteEntry> m_sortedNormalEntries;
    bool m_noteCacheValid = false;
    QSet<int> m_hyperIndices;
    QVector<bool> m_hyperMask;
    bool m_hyperCacheValid = false;
    QTimer *m_deferredUpdateTimer = nullptr;
    QElapsedTimer m_frameTimer;
    bool m_updateScheduled = false;
    qint64 m_lastPlaybackFrameSeq = -1;
};

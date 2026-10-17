#pragma once
#include "analysis/MusicalTimeTransform.h"
#include "audio/SpectrumService.h"
#include <QObject>
#include <QTimer>
#include <QRectF>
#include <QSet>

class ChartCanvas;
class ChartController;
class PlaybackController;
class QPainter;

// Passive audio layer: the canvas supplies its beat projection and playback
// scheduler. This object only requests immutable audio pages and paints them.
class ChartSpectrumOverlay : public QObject
{
    Q_OBJECT
public:
    using Runner = std::function<void(QObject *, const QString &, double, double,
                                      std::shared_ptr<std::atomic<bool>>, SpectrumService::PageCallback)>;
    explicit ChartSpectrumOverlay(ChartCanvas *, Runner = {});
    ~ChartSpectrumOverlay() override;
    void setChartController(ChartController *);
    void setPlaybackController(PlaybackController *);
    void paint(QPainter &, const QRectF &);
    const SpectrumService::Page &currentPage() const { return m_current; }

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    void schedule();
    void refreshSource();
    void refreshTiming();
    void request();
    void suspend();
    ChartCanvas *m_canvas;
    ChartController *m_chart = nullptr;
    PlaybackController *m_playback = nullptr;
    Runner m_runner;
    QTimer m_requestTimer, m_sourceTimer;
    QVector<MathUtils::BpmCacheEntry> m_timings;
    analysis::MusicalTimeTransform m_transform;
    QVector<SpectrumService::Page> m_pages;
    SpectrumService::Page m_current;
    QString m_path, m_identity;
    QSet<int> m_failedTiles;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    quint64 m_generation = 0;
    double m_pendingStart = 0, m_pendingEnd = 0, m_eof = -1;
    bool m_busy = false;
};

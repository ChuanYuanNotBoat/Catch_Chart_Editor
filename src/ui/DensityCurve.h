#pragma once

#include <QWidget>
#include <QVector>

class Chart;
class ChartController;
class PlaybackController;
class ChartCanvas;

class DensityCurve : public QWidget
{
    Q_OBJECT
public:
    explicit DensityCurve(QWidget *parent = nullptr);
    void setChartController(ChartController *controller);
    void setPlaybackController(PlaybackController *controller);
    void setCanvas(ChartCanvas *canvas);

signals:
    void seekPreviewRequested(double timeMs);
    void seekRequested(double timeMs);
    void seekGestureStarted();
    void seekGestureFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    static QString formatTimeMs(double timeMs);
    void syncDuration();
    void syncCurrentTime(double timeMs);
    void refreshFromChart();
    void scheduleRefreshFromChart();
    void updateFromCanvasBeat(double beat);
    void updateFromPointer(const QPoint &pos, bool commitSeek);
    void computeDensity();

    static constexpr int kBinCount = 80;
    static constexpr int kRefMaxCount = 20;

    ChartController *m_chartController;
    quint64 m_chartRevision = 0;
    quint64 m_densityRevision = 0;
    PlaybackController *m_playbackController;
    ChartCanvas *m_canvas;
    const Chart *m_chart;
    QVector<int> m_densityData;
    double m_currentTimeMs;
    double m_durationMs;
    double m_tipTimeMs;
    bool m_dragging;
    bool m_showTip;
    bool m_refreshScheduled = false;
};

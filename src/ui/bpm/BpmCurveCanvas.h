#pragma once
#include "ui/analysis/TimingEditorCanvas.h"
#include "utils/MathUtils.h"
#include "model/BpmEntry.h"

class ChartController;
class ChartCanvas;

class BpmCurveCanvas : public TimingEditorCanvas
{
    Q_OBJECT
public:
    BpmCurveCanvas(ChartController *, ChartCanvas *, QWidget *parent = nullptr);
    double timeAtBeat(double beat) const override;
    double timeAtY(double y) const;
    double yAtBeat(double beat) const;
    void setMeasurementPicking(bool) override;
    void setMeasurementOverlay(std::optional<double>, std::optional<double>, double, const QString &) override;
    void setInterpolationStartPicking(bool) override;
    void setInterpolationCurve(const analysis::InterpolationOptions &, const QVector<double> &) override;
    void setInterpolationPreview(const analysis::TimingInterpolation &, double) override;
    void clearInterpolationPreview() override;
    void cancelPicking();
    void selectTiming(int index);
    bool interpolationPreviewVisible() const { return m_interpolation.valid(); }
    int lastPaintSamples() const { return m_lastPaintSamples; }
signals:
    void timingSelected(int index);
    void seekRequested(double audioMs);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    void rebuildTiming();
    double beatAtY(double y) const;
    double yAtTime(double ms) const;
    double beatAtTime(double ms) const;
    int nearestTiming(double y) const;
    void pickStart(double y);
    ChartController *m_chart;
    ChartCanvas *m_main;
    QVector<BpmEntry> m_entries;
    QVector<MathUtils::BpmCacheEntry> m_timeCache;
    analysis::TimingInterpolation m_interpolation;
    analysis::InterpolationOptions m_options;
    QVector<double> m_stored;
    std::optional<double> m_measureStart, m_measureEnd;
    double m_measureBpm = 0, m_minBpm = 100, m_maxBpm = 200;
    bool m_measurePicking = false, m_interpolationPicking = false;
    int m_dragging = 0, m_selected = -1, m_lastPaintSamples = 0;
    static constexpr int topMargin = 62;
};

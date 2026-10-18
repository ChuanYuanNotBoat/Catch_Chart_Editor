#pragma once
#include <QWidget>
#include <QVector>
#include <optional>
#include "analysis/TimingInterpolation.h"

// The timing tools share picking and preview semantics without depending on
// spectrum decoding, note rendering, or the analysis workbench.
class TimingEditorCanvas : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual double timeAtBeat(double beat) const = 0;
    virtual void setMeasurementPicking(bool) = 0;
    virtual void setMeasurementOverlay(std::optional<double>, std::optional<double>, double, const QString &) = 0;
    virtual void setInterpolationStartPicking(bool) = 0;
    virtual void setInterpolationPreview(const analysis::TimingInterpolation &, double startMs) = 0;
    virtual void clearInterpolationPreview() = 0;
    virtual void setInterpolationCurve(const analysis::InterpolationOptions &, const QVector<double> &) {}
signals:
    void measurementStartRequested(int whole, int numerator, int denominator);
    void measurementEndRequested(double milliseconds);
    void measurementCancelRequested();
    void measurementDetailsRequested();
    void interpolationStartRequested(int whole, int numerator, int denominator);
    void interpolationPickCancelled();
};

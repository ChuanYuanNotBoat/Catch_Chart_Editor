#pragma once
#include <QWidget>
#include <QJsonObject>
#include <optional>
#include "model/BpmEntry.h"
#include "analysis/TimingMeasurement.h"

class ChartController;
class AnalysisCanvas;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

class TimingToolsPanel : public QWidget
{
    Q_OBJECT
public:
    TimingToolsPanel(ChartController *, AnalysisCanvas *, QWidget *parent = nullptr);
    void clearMeasurement();
    void stopPicking();
    QJsonObject diagnostics() const;
signals:
    void pickingStarted();
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    void setStart(int whole, int numerator, int denominator);
    void setEnd(double milliseconds);
    void refresh();
    void apply();
    int matchingTiming() const;
    double startTime() const;
    ChartController *m_chart;
    AnalysisCanvas *m_canvas;
    QLabel *m_start, *m_summary, *m_effect;
    QLineEdit *m_span;
    QDoubleSpinBox *m_end;
    QCheckBox *m_pick, *m_preview;
    QPushButton *m_apply, *m_copy;
    std::optional<BpmEntry> m_startBeat;
    std::optional<double> m_endMs;
    analysis::TimingMeasurement m_result;
};

#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QVector>
#include <array>
#include "analysis/TimingInterpolation.h"
#include "model/BpmEntry.h"

class ChartController;
class AnalysisCanvas;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QTimer;

class TimingInterpolationPanel : public QWidget
{
    Q_OBJECT
public:
    TimingInterpolationPanel(ChartController *, AnalysisCanvas *, QWidget *parent = nullptr);
    void stopPicking();
    QJsonObject diagnostics() const;
signals:
    void pickingStarted();
protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    analysis::BeatTriplet readBeat(const std::array<QSpinBox *, 3> &) const;
    void setStart(int whole, int numerator, int denominator);
    void schedule();
    void invalidate();
    void buildPreview();
    void displayPreview();
    void apply();
    void exportJson();
    ChartController *m_chart;
    AnalysisCanvas *m_canvas;
    std::array<QSpinBox *, 3> m_start, m_end;
    QDoubleSpinBox *m_startBpm, *m_endBpm, *m_duration, *m_error;
    QLineEdit *m_length, *m_gap;
    QComboBox *m_mode;
    QCheckBox *m_pick, *m_resume, *m_preview;
    QPushButton *m_apply, *m_export;
    QLabel *m_summary;
    QTableWidget *m_table;
    QTimer *m_timer;
    analysis::InterpolationOptions m_options;
    analysis::TimingInterpolation m_result;
    QVector<BpmEntry> m_proposal;
    QVector<double> m_storedBpms;
    quint64 m_revision = 0;
    double m_startMs = 0, m_cacheError = 0, m_durationResidual = 0;
    int m_replaced = 0;
    QString m_failure;
};

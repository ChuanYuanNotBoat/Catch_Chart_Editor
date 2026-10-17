#pragma once
#include <QWidget>
#include <QFutureWatcher>
#include <QJsonObject>
#include <memory>
#include "analysis/TransientAnalysis.h"
#include "analysis/AnalysisTracks.h"
class AnalysisCanvas;
class QDoubleSpinBox;
class QCheckBox;
class QTableWidget;
class QLabel;
class QTimer;
class TransientPlot;
class TransientPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TransientPanel(AnalysisCanvas *, QWidget *parent = nullptr);
    ~TransientPanel() override;
    void setSpectrum(const analysis::StereoSpectrum &);
    void setSpectrum(std::shared_ptr<const analysis::StereoSpectrum>);
    void clearSource();
    const analysis::TransientResult &result() const
    {
        return m_result;
    }
    bool busy() const
    {
        return m_busy || m_pending;
    }
    QJsonObject diagnostics() const;
    QVector<analysis::AnalysisTrack> tracks() const;
    void selectPeakAt(double seconds);
signals:
    void seekRequested(double milliseconds);
    void tracksChanged();

private:
    void scheduleAnalysis();
    void startAnalysis();
    void fillPeaks();
    void exportResult();
    AnalysisCanvas *m_canvas;
    TransientPlot *m_plot;
    QDoubleSpinBox *m_threshold, *m_interval;
    QCheckBox *m_rejected;
    QTableWidget *m_peaks;
    QLabel *m_summary;
    QTimer *m_timer;
    std::shared_ptr<const analysis::StereoSpectrum> m_spectrum;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    analysis::TransientResult m_result;
    quint64 m_generation = 0;
    bool m_busy = false, m_pending = false;
};

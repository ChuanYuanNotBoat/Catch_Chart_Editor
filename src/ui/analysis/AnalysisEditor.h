#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QHash>
#include <memory>
#include <atomic>
#include "audio/BpmDetector.h"
#include "analysis/AnalysisSession.h"
#include "audio/SpectrumService.h"
class AnalysisCanvas;
class ChartController;
class SelectionController;
class PlaybackController;
class ChartCanvas;
class LongRangeSelector;
class QSplitter;
class QTabBar;
class QStackedWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class QTableWidget;
class QTabWidget;
class QPlainTextEdit;
class QTimer;
class CommandRouter;
class TransientPanel;
class TimingToolsPanel;
class TimingInterpolationPanel;
class AnalysisConfigPanel;
class AnalysisWorkbench;
class AudioOverview;
class QComboBox;
// A second workspace over the same chart/playback controllers, not a chart copy.
class AnalysisEditor : public QWidget
{
    Q_OBJECT
  public:
      using SpectrumRunner = std::function<void(QObject *, const QString &, double, double,
                                                std::shared_ptr<std::atomic<bool>>, SpectrumService::PageCallback)>;
      AnalysisEditor(ChartController *, SelectionController *, PlaybackController *, ChartCanvas *, LongRangeSelector *,
                     QWidget *parent = nullptr, SpectrumRunner = {});
      ~AnalysisEditor() override;
      AnalysisCanvas *canvas() const
      {
          return m_canvas;
      }
    // Small extension point for future numeric/debug panels; never overlays the spectrum.
    void addAnalysisPanel(const QString &title, QWidget *panel, bool rightSide = false);
    void showTimingResult(const BpmDetector::DetectionResult &, const QString &pipelineError = {});
    QJsonObject resultDiagnostics() const
    {
        return m_diagnostics;
    }

  protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    QWidget *createTimingPanel();
    QWidget *createDiagnosticsPanel();
    void togglePanel(bool right, int index);
    void updateTime(double ms);
    void seek(double ms);
    void updateRange();
    void updateLoopFromRange();
    void refreshAudioSource();
    void scheduleSpectrum();
    void requestSpectrum();
    void runTiming();
    void cancelTiming();
    void clearDiagnostics();
    void saveLayout();
    void restoreLayout();
    void exportDiagnostics();
    void fillTable(const QString &, const QJsonArray &);
    void selectDiagnosticWindow(const QJsonObject &);
    void selectTimingCandidate(const QJsonObject &);
    void updateTimingPreview();
    void invalidateTimingConfiguration();
    void setInterfaceLevel(int);
    void updateAnalysisDisplay();
    void inspectHypothesis(int);
    void requestOverview();
    void applyTempoMap();
    ChartController *m_chart;
    SelectionController *m_selection;
    PlaybackController *m_playback;
    ChartCanvas *m_main;
    LongRangeSelector *m_range;
    AnalysisCanvas *m_canvas;
    TransientPanel *m_transient;
    TimingToolsPanel *m_timingTools;
    TimingInterpolationPanel *m_interpolation;
    AnalysisConfigPanel *m_configPanel;
    AnalysisWorkbench *m_workbench = nullptr;
    AudioOverview *m_overview;
    QComboBox *m_level, *m_timeView;
    QTabWidget *m_timingTables;
    QCheckBox *m_preferLocal, *m_modelGrid, *m_showTracks;
    QPushButton *m_applyMap;
    analysis::AnalysisSession m_session;
    BpmDetector::DetectionResult m_result;
    AutoTiming2TempoMap m_inspectedMap;
    QJsonObject m_inspectedDiagnostics;
    QJsonObject m_selectedEvidence;
    int m_interfaceLevel = 0;
    std::shared_ptr<std::atomic<bool>> m_overviewCancel;
    bool m_overviewBusy = false;
    QSplitter *m_panels, *m_work;
    QTabBar *m_leftTabs, *m_rightTabs;
    QStackedWidget *m_leftPanels, *m_rightPanels;
    QLabel *m_time, *m_status, *m_timingSummary, *m_candidateDetails, *m_sourceName;
    QPushButton *m_play, *m_runTiming, *m_cancelTiming, *m_candidateSeek;
    QCheckBox *m_loop, *m_syncView, *m_complexSubdivision, *m_candidateGrid;
    QDoubleSpinBox *m_zoom, *m_timingStart, *m_timingDuration;
    QPlainTextEdit *m_raw;
    QHash<QString, QTableWidget *> m_tables;
    QTimer *m_spectrumTimer;
    CommandRouter *m_router;
    QJsonObject m_diagnostics;
    QJsonObject m_selectedCandidate;
    QString m_audioPath, m_sourceIdentity, m_spectrumFailureKey;
    std::shared_ptr<std::atomic<bool>> m_spectrumCancel;
    SpectrumRunner m_spectrumRunner;
    QVector<SpectrumService::Page> m_spectrumPages;
    quint64 m_sourceGeneration = 0, m_spectrumGeneration = 0;
    double m_pendingStart = 0, m_pendingDuration = 0, m_cachedEofMs = -1;
    bool m_timingBusy = false, m_spectrumBusy = false, m_restored = false;
    int m_leftOpen = -1, m_rightOpen = -1;
};

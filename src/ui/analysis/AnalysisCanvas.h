#pragma once
#include <QWidget>
#include <QImage>
#include <optional>
#include "analysis/SpectrumAnalysis.h"
#include "analysis/TimingInterpolation.h"
#include "model/Note.h"
#include "render/RainVisibilityIndex.h"
#include "utils/MathUtils.h"
#include "analysis/MusicalTimeTransform.h"
#include "analysis/AnalysisSession.h"
#include "AnalysisTrackScene.h"
#include "SpectrumViewport.h"
#include <memory>
class ChartController;
class SelectionController;
class ChartCanvas;
// Compact time-only canvas. Decoder, algorithms, and panels live elsewhere.
class AnalysisCanvas : public QWidget
{
    Q_OBJECT
public:
    AnalysisCanvas(ChartController *, SelectionController *, ChartCanvas *, QWidget *parent = nullptr);
    double timeAtY(double y) const;
    double yAtTime(double ms) const;
    double beatAtTime(double ms) const;
    double timeAtBeat(double beat) const;
    double currentTime() const
    {
        return m_current;
    }
    double millisecondsPerPixel() const
    {
        return m_msPerPixel;
    }
    bool musicalView() const
    {
        return m_musicalView;
    }
    double beatsPerPixel() const
    {
        return m_beatsPerPixel;
    }
    void setMusicalView(bool enabled);
    void setBeatsPerPixel(double value);
    void setTempoMapPreview(const AutoTiming2TempoMap &);
    void setAnalysisTracks(const QVector<analysis::AnalysisTrack> &, const QJsonArray &regions);
    const QVector<analysis::AnalysisTrack> &analysisTracks() const
    {
        return m_trackScene.tracks();
    }
    void deleteSelectedNotes();
    int noteLaneWidth() const
    {
        return m_noteLaneWidth;
    }
    int spectrumWidth() const;
    bool viewSynchronized() const
    {
        return m_syncView;
    }
    const analysis::StereoSpectrum &spectrum() const
    {
        static const analysis::StereoSpectrum empty;
        return m_spectrum ? *m_spectrum : empty;
    }
    Note snappedNoteAtY(double y) const;
    void setCurrentTime(double ms);
    void setMillisecondsPerPixel(double value);
    void setNoteLaneWidth(int width);
    void setViewSynchronized(bool enabled);
    void setRainMode(bool enabled);
    void setSpectrum(analysis::StereoSpectrum data);
    void setSpectrumPage(std::shared_ptr<const analysis::StereoSpectrum>, analysis::SpectrumRaster);
    quint64 spectrumViewportBuildCount() const
    {
        return m_spectrumViewport.rebuildCount();
    }
    void clearSpectrum();
    void setRange(double startBeat, double endBeat, bool visible = true);
    void setLoop(double startMs, double endMs, bool enabled);
    void setTimingPreview(double bpm, double pulseMs, double startMs, double endMs);
    void clearTimingPreview();
    void setMeasurementPicking(bool enabled);
    void setMeasurementOverlay(std::optional<double> startMs, std::optional<double> endMs, double bpm,
                               const QString &startLabel);
    bool measurementPicking() const
    {
        return m_measurePicking;
    }
    void setInterpolationStartPicking(bool enabled);
    void setInterpolationPreview(const analysis::TimingInterpolation &, double startMs);
    void clearInterpolationPreview();
    bool interpolationPreviewVisible() const
    {
        return m_interpolationPreview.valid();
    }
    bool timingPreviewVisible() const
    {
        return m_timingPreview.has_value();
    }
    void cancelGesture();
    void setStatus(const QString &text);
signals:
    void analysisObjectSelected(QString trackId, QString handle, QJsonObject details);
    void seekRequested(double ms);
    void rangeRequested(double startBeat, double endBeat);
    void viewportChanged();
    void statusMessage(const QString &text);
    void measurementStartRequested(int whole, int numerator, int denominator);
    void measurementEndRequested(double milliseconds);
    void measurementCancelRequested();
    void measurementDetailsRequested();
    void interpolationStartRequested(int whole, int numerator, int denominator);
    void interpolationPickCancelled();

protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void rebuildChartCache();
    void alignViewport();
    int hitNote(double y) const;
    void drawSpectrum(QPainter &);
    void drawTiming(QPainter &);
    void drawTimingPreview(QPainter &);
    void drawAnalysis(QPainter &);
    void drawMeasurement(QPainter &);
    void drawInterpolation(QPainter &);
    void pickMeasurementStart(double y);
    void drawNotes(QPainter &);
    ChartController *m_chart;
    SelectionController *m_selection;
    ChartCanvas *m_main;
    QVector<MathUtils::BpmCacheEntry> m_bpm;
    analysis::MusicalTimeTransform m_transform;
    bool m_musicalView = false;
    double m_beatsPerPixel = .012, m_viewBeatStart = 0;
    AutoTiming2TempoMap m_modelPreview;
    AnalysisTrackScene m_trackScene;
    QVector<double> m_starts, m_ends;
    RainVisibilityIndex::IntervalIndex m_noteIndex;
    std::shared_ptr<const analysis::StereoSpectrum> m_spectrum;
    analysis::SpectrumRaster m_spectrumRaster;
    SpectrumViewport m_spectrumViewport;
    quint64 m_spectrumRevision = 0;
    double m_current = 0, m_viewStart = 0, m_msPerPixel = 6;
    int m_noteLaneWidth = 56;
    double m_rangeStart = 0, m_rangeEnd = 0, m_loopStart = 0, m_loopEnd = 0, m_rangeAnchor = 0;
    bool m_rangeVisible = false, m_loopEnabled = false, m_syncView = false, m_rainMode = false;
    bool m_resizeDivider = false, m_selectingRange = false;
    std::optional<Note> m_rainAnchor, m_tailOriginal, m_tailPreview;
    std::optional<Note> m_moveOriginal, m_movePreview;
    quint64 m_gestureRevision = 0;
    struct TimingPreview
    {
        double bpm, pulseMs, startMs, endMs;
    };
    std::optional<TimingPreview> m_timingPreview;
    std::optional<double> m_measureStart, m_measureEnd;
    double m_measureBpm = 0;
    QString m_measureStartLabel;
    bool m_measurePicking = false;
    int m_measureDragging = 0;
    bool m_interpolationPicking = false;
    analysis::TimingInterpolation m_interpolationPreview;
    double m_interpolationStartMs = 0;
    QString m_status;
    static constexpr int headerHeight = 28;
};

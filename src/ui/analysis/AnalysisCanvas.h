#pragma once
#include <QWidget>
#include <QImage>
#include <optional>
#include "analysis/SpectrumAnalysis.h"
#include "model/Note.h"
#include "render/RainVisibilityIndex.h"
#include "utils/MathUtils.h"
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
    double currentTime() const { return m_current; }
    double millisecondsPerPixel() const { return m_msPerPixel; }
    double spectrumFraction() const { return m_spectrumFraction; }
    int spectrumWidth() const;
    bool viewSynchronized() const { return m_syncView; }
    const analysis::StereoSpectrum &spectrum() const { return m_spectrum; }
    Note snappedNoteAtY(double y) const;
    void setCurrentTime(double ms);
    void setMillisecondsPerPixel(double value);
    void setSpectrumFraction(double fraction);
    void setViewSynchronized(bool enabled);
    void setRainMode(bool enabled);
    void setSpectrum(analysis::StereoSpectrum data);
    void clearSpectrum();
    void setRange(double startBeat, double endBeat, bool visible = true);
    void setLoop(double startMs, double endMs, bool enabled);
    void cancelGesture();
    void setStatus(const QString &text);
  signals:
    void seekRequested(double ms);
    void rangeRequested(double startBeat, double endBeat);
    void viewportChanged();
    void statusMessage(const QString &text);

  protected:
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
    void drawNotes(QPainter &);
    ChartController *m_chart;
    SelectionController *m_selection;
    ChartCanvas *m_main;
    QVector<MathUtils::BpmCacheEntry> m_bpm;
    QVector<double> m_starts, m_ends;
    RainVisibilityIndex::IntervalIndex m_noteIndex;
    analysis::StereoSpectrum m_spectrum;
    QImage m_leftImage, m_rightImage;
    double m_current = 0, m_viewStart = 0, m_msPerPixel = 6, m_spectrumFraction = 0.72;
    double m_rangeStart = 0, m_rangeEnd = 0, m_loopStart = 0, m_loopEnd = 0, m_rangeAnchor = 0;
    bool m_rangeVisible = false, m_loopEnabled = false, m_syncView = false, m_rainMode = false;
    bool m_resizeDivider = false, m_selectingRange = false;
    std::optional<Note> m_rainAnchor, m_tailOriginal, m_tailPreview;
    QString m_status;
    static constexpr int headerHeight = 28;
};

#pragma once
#include <QWidget>
#include "audio/SpectrumService.h"
class AnalysisCanvas;
class AudioOverview : public QWidget
{
    Q_OBJECT
public:
    explicit AudioOverview(AnalysisCanvas *, QWidget *parent = nullptr);
    void setEnvelope(SpectrumService::EnergyEnvelope);
    void setStatus(const QString &);
    void setRange(double startMs, double endMs, bool visible);
    void setLoop(double startMs, double endMs, bool enabled);
    bool hasEnvelope() const
    {
        return !m_data.bins.isEmpty();
    }
signals:
    void seekRequested(double milliseconds);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    double timeAt(double y) const;
    double yAt(double seconds) const;
    AnalysisCanvas *m_canvas;
    SpectrumService::EnergyEnvelope m_data;
    QString m_status;
    double m_rangeStart = 0, m_rangeEnd = 0, m_loopStart = 0, m_loopEnd = 0;
    bool m_rangeVisible = false, m_loopEnabled = false;
};

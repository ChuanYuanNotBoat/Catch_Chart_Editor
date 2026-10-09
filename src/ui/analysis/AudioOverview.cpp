#include "AudioOverview.h"
#include "AnalysisCanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
AudioOverview::AudioOverview(AnalysisCanvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas)
{
    setObjectName("analysis.audioOverview");
    setMinimumWidth(56);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setToolTip(tr("Whole-audio relative energy (RMS / peak), not Note density or LUFS. Click or drag to navigate."));
    connect(canvas, &AnalysisCanvas::viewportChanged, this, qOverload<>(&QWidget::update));
}
void AudioOverview::setEnvelope(SpectrumService::EnergyEnvelope data)
{
    m_data = std::move(data);
    m_status = m_data.error;
    update();
}
void AudioOverview::setStatus(const QString &text)
{
    m_status = text;
    update();
}
void AudioOverview::setRange(double startMs, double endMs, bool visible)
{
    m_rangeStart = startMs;
    m_rangeEnd = endMs;
    m_rangeVisible = visible;
    update();
}
void AudioOverview::setLoop(double startMs, double endMs, bool enabled)
{
    m_loopStart = startMs;
    m_loopEnd = endMs;
    m_loopEnabled = enabled;
    update();
}
double AudioOverview::yAt(double seconds) const
{
    double fraction = m_data.durationSeconds > 0 ? seconds / m_data.durationSeconds : 0;
    if (m_canvas->musicalView())
    {
        double start = m_canvas->beatAtTime(0), end = m_canvas->beatAtTime(m_data.durationSeconds * 1000);
        if (end > start)
            fraction = (m_canvas->beatAtTime(seconds * 1000) - start) / (end - start);
    }
    if (m_canvas->timeAtY(28) > m_canvas->timeAtY(m_canvas->height()))
        fraction = 1 - fraction;
    return 28 + fraction * qMax(1, height() - 52);
}
double AudioOverview::timeAt(double y) const
{
    double fraction = qBound(0., (y - 28) / qMax(1, height() - 52), 1.);
    if (m_canvas->timeAtY(28) > m_canvas->timeAtY(m_canvas->height()))
        fraction = 1 - fraction;
    if (m_canvas->musicalView())
    {
        double start = m_canvas->beatAtTime(0), end = m_canvas->beatAtTime(m_data.durationSeconds * 1000);
        return m_canvas->timeAtBeat(start + fraction * (end - start));
    }
    return fraction * m_data.durationSeconds * 1000;
}
void AudioOverview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int contentWidth = qMin(width(), 80);
    p.fillRect(rect(), palette().window());
    p.setPen(palette().text().color());
    p.drawText(QRect(2, 0, contentWidth - 4, 24), Qt::AlignCenter, tr("Audio energy"));
    if (!hasEnvelope())
    {
        p.drawText(QRect(5, 30, contentWidth - 10, height() - 35), Qt::AlignTop | Qt::TextWordWrap,
                   m_status.isEmpty() ? tr("Link audio for whole-track navigation.") : m_status);
        return;
    }
    double maxRms = .000001, maxPeak = .000001;
    for (auto b : m_data.bins)
    {
        maxRms = qMax(maxRms, b.rms);
        maxPeak = qMax(maxPeak, b.peak);
    }
    const double w = qMax(1, contentWidth - 12);
    p.setClipRect(QRect(2, 28, contentWidth - 4, height() - 52));
    for (auto b : m_data.bins)
    {
        double a = yAt(b.startSeconds), z = yAt(b.endSeconds);
        p.fillRect(QRectF(6, qMin(a, z), w * std::sqrt(b.peak / maxPeak), qMax(1., qAbs(z - a))),
                   QColor(85, 128, 151, 100));
        p.fillRect(QRectF(6, qMin(a, z), w * std::sqrt(b.rms / maxRms), qMax(1., qAbs(z - a))),
                   QColor(97, 190, 172, 160));
    }
    auto region = [&](double startMs, double endMs, const QColor &color) {
        if (endMs <= startMs)
            return;
        const double a = yAt(startMs / 1000), b = yAt(endMs / 1000);
        p.fillRect(QRectF(contentWidth - 10, qMin(a, b), 6, qMax(2., qAbs(b - a))), color);
    };
    if (m_rangeVisible)
        region(m_rangeStart, m_rangeEnd, QColor(118, 155, 245, 220));
    if (m_loopEnabled)
        region(m_loopStart, m_loopEnd, QColor(211, 153, 240, 240));
    double a = yAt(m_canvas->timeAtY(28) / 1000), b = yAt(m_canvas->timeAtY(m_canvas->height()) / 1000);
    p.setPen(QPen(QColor(245, 213, 110), 1.5));
    p.setBrush(QColor(245, 213, 110, 25));
    p.drawRect(QRectF(3, qMin(a, b), contentWidth - 6, qMax(2., qAbs(b - a))));
    p.setPen(QPen(QColor(250, 116, 112), 1.5));
    p.drawLine(QPointF(0, yAt(m_canvas->currentTime() / 1000)),
               QPointF(contentWidth, yAt(m_canvas->currentTime() / 1000)));
    p.setClipping(false);
    p.setPen(palette().text().color());
    p.drawText(QRect(0, height() - 22, contentWidth, 22), Qt::AlignCenter,
               tr("%1 s").arg(m_data.durationSeconds, 0, 'f', 1));
}
void AudioOverview::mousePressEvent(QMouseEvent *event)
{
    if (hasEnvelope() && event->position().x() <= 80 && event->button() == Qt::LeftButton)
        emit seekRequested(qMax(0., timeAt(event->position().y())));
}
void AudioOverview::mouseMoveEvent(QMouseEvent *event)
{
    if (hasEnvelope() && event->position().x() <= 80 && (event->buttons() & Qt::LeftButton))
        emit seekRequested(qMax(0., timeAt(event->position().y())));
}

#include "BpmCurveCanvas.h"
#include "controller/ChartController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QShowEvent>
#include <algorithm>
#include <cmath>

BpmCurveCanvas::BpmCurveCanvas(ChartController *chart, ChartCanvas *main, QWidget *parent)
    : TimingEditorCanvas(parent), m_chart(chart), m_main(main)
{
    setObjectName("bpm.curve");
    setMinimumSize(280, 240);
    setFocusPolicy(Qt::StrongFocus);
    connect(chart, &ChartController::bpmListChanged, this, &BpmCurveCanvas::rebuildTiming);
    connect(chart, &ChartController::metaDataChanged, this, &BpmCurveCanvas::rebuildTiming);
    connect(chart, &ChartController::chartLoaded, this, &BpmCurveCanvas::rebuildTiming);
    for (auto signal : {&ChartCanvas::scrollPositionChanged, &ChartCanvas::timeScaleChanged,
                        &ChartCanvas::currentTimeChanged})
        connect(main, signal, this, [this] { if (isVisible()) update(); });
    connect(main, &ChartCanvas::verticalFlipChanged, this, qOverload<>(&QWidget::update));
    rebuildTiming();
}
void BpmCurveCanvas::rebuildTiming()
{
    m_entries = m_chart->chart()->bpmList();
    m_timeCache = MathUtils::buildBpmTimeCache(m_entries, m_chart->chart()->meta().offset);
    m_minBpm = 10000; m_maxBpm = 0;
    for (const auto &entry : m_entries)
        if (std::isfinite(entry.bpm) && entry.bpm > 0)
        {
            m_minBpm = qMin(m_minBpm, entry.bpm);
            m_maxBpm = qMax(m_maxBpm, entry.bpm);
        }
    if (m_maxBpm == 0) { m_minBpm = 100; m_maxBpm = 200; }
    m_selected = -1;
    update();
}
double BpmCurveCanvas::timeAtBeat(double beat) const
{
    if (m_timeCache.isEmpty()) return 0;
    auto it = std::upper_bound(m_timeCache.cbegin(), m_timeCache.cend(), beat,
        [](double b, const auto &entry) { return b < entry.beatPos; });
    if (it != m_timeCache.cbegin()) --it;
    return it->bpm > 0 ? it->accumulatedMs + (beat - it->beatPos) * 60000 / it->bpm : it->accumulatedMs;
}
double BpmCurveCanvas::beatAtTime(double ms) const
{
    if (m_timeCache.isEmpty()) return 0;
    auto it = std::upper_bound(m_timeCache.cbegin(), m_timeCache.cend(), ms,
        [](double t, const auto &entry) { return t < entry.accumulatedMs; });
    if (it != m_timeCache.cbegin()) --it;
    return it->beatPos + (ms - it->accumulatedMs) * it->bpm / 60000;
}
double BpmCurveCanvas::beatAtY(double y) const
{
    return m_main->chartYToBeat((y - topMargin) / qMax(1, height() - topMargin) * m_main->height());
}
double BpmCurveCanvas::timeAtY(double y) const { return timeAtBeat(beatAtY(y)); }
double BpmCurveCanvas::yAtBeat(double beat) const
{
    return topMargin + m_main->chartBeatToY(beat) / qMax(1, m_main->height()) * (height() - topMargin);
}
double BpmCurveCanvas::yAtTime(double ms) const { return yAtBeat(beatAtTime(ms)); }
void BpmCurveCanvas::setMeasurementPicking(bool enabled)
{
    m_measurePicking = enabled; m_dragging = 0;
    if (enabled) m_interpolationPicking = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}
void BpmCurveCanvas::setMeasurementOverlay(std::optional<double> start, std::optional<double> end,
                                          double bpm, const QString &)
{
    m_measureStart = start; m_measureEnd = end; m_measureBpm = bpm; update();
}
void BpmCurveCanvas::setInterpolationStartPicking(bool enabled)
{
    m_interpolationPicking = enabled;
    if (enabled) m_measurePicking = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}
void BpmCurveCanvas::setInterpolationCurve(const analysis::InterpolationOptions &options, const QVector<double> &stored)
{
    m_options = options; m_stored = stored;
}
void BpmCurveCanvas::setInterpolationPreview(const analysis::TimingInterpolation &result, double)
{
    m_interpolation = result; update();
}
void BpmCurveCanvas::clearInterpolationPreview() { m_interpolation = {}; m_stored.clear(); update(); }
void BpmCurveCanvas::cancelPicking()
{
    setMeasurementPicking(false); setInterpolationStartPicking(false);
    emit measurementCancelRequested(); emit interpolationPickCancelled();
}
void BpmCurveCanvas::selectTiming(int index)
{
    m_selected = index >= 0 && index < m_entries.size() ? index : -1;
    update();
}
int BpmCurveCanvas::nearestTiming(double y) const
{
    const double beat = beatAtY(y);
    const auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), beat,
        [](const BpmEntry &point, double b) { return point.position().toDouble() < b; });
    const int index = int(it - m_entries.cbegin());
    int best = -1; double distance = 6;
    for (int i : {index - 1, index})
        if (i >= 0 && i < m_entries.size())
        {
            const double d = std::abs(yAtBeat(m_entries[i].position().toDouble()) - y);
            if (d < distance) { distance = d; best = i; }
        }
    return best;
}
void BpmCurveCanvas::pickStart(double y)
{
    int b, n, d;
    const int nearest = nearestTiming(y);
    if (nearest >= 0)
    {
        const auto &point = m_entries[nearest]; b = point.beatNum; n = point.numerator; d = point.denominator;
    }
    else
    {
        if (!MathUtils::quantizeBeatToDivision(qMax(0., beatAtY(y)), m_main->timeDivision(), b, n, d)) return;
        const auto position = BeatPosition(b, n, d);
        const auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), position,
            [](const auto &point, const auto &p) { return point.position() < p; });
        if (it != m_entries.cend() && it->position() == position)
        { b = it->beatNum; n = it->numerator; d = it->denominator; }
    }
    if (m_interpolationPicking) emit interpolationStartRequested(b, n, d);
    else emit measurementStartRequested(b, n, d);
}
void BpmCurveCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    double minimum = m_minBpm, maximum = m_maxBpm;
    if (m_interpolation.valid())
    { minimum = qMin(minimum, qMin(m_options.startBpm, m_options.endBpm)); maximum = qMax(maximum, qMax(m_options.startBpm, m_options.endBpm)); }
    if (m_measureBpm > 0) { minimum = qMin(minimum, m_measureBpm); maximum = qMax(maximum, m_measureBpm); }
    const double padding = qMax(5., (maximum - minimum) * .08);
    minimum = qMax(0., minimum - padding); maximum += padding;
    const auto x = [&](double bpm) { return 80. + (width() - 100.) * (bpm - minimum) / (maximum - minimum); };
    const QColor actual(90, 180, 250), ideal(255, 190, 80), stored(90, 220, 150), measured(205, 140, 250);
    p.setPen(actual); p.drawText(10, 18, tr("Chart BPM"));
    p.setPen(ideal); p.drawText(130, 18, tr("Ideal curve"));
    p.setPen(stored); p.drawText(250, 18, tr("Stored points"));
    p.setPen(palette().text().color());
    p.drawText(80, 42, QString::number(minimum, 'f', 1));
    p.drawText(QRectF(150, 26, width() - 170, 20), Qt::AlignRight, tr("%1 BPM").arg(maximum, 0, 'f', 1));
    p.save(); p.setClipRect(QRect(0, topMargin, width(), height() - topMargin));
    const double low = qMin(beatAtY(topMargin), beatAtY(height())), high = qMax(beatAtY(topMargin), beatAtY(height()));
    p.setPen(QColor(80, 85, 95));
    const double step = std::pow(2., std::ceil(std::log2(qMax(.25, (high - low) / 16.))));
    for (double beat = std::ceil(low / step) * step; beat <= high; beat += step)
    {
        const double y = yAtBeat(beat);
        p.drawLine(QPointF(75, y), QPointF(width(), y));
        p.drawText(QRectF(0, y - 9, 70, 18), Qt::AlignRight, QString::number(beat, 'g', 8));
    }
    // Pixel sampling bounds paint work independently of chart timing density.
    // Exact entries remain authoritative for selection, picking and application.
    QPainterPath chartPath, previewPath, curvePath;
    m_lastPaintSamples = 0;
    bool chartBegun = false, previewBegun = false, curveBegun = false;
    for (int y = topMargin; y <= height(); ++y)
    {
        ++m_lastPaintSamples;
        const double beat = beatAtY(y);
        auto it = std::upper_bound(m_timeCache.cbegin(), m_timeCache.cend(), beat,
            [](double b, const auto &point) { return b < point.beatPos; });
        if (it != m_timeCache.cbegin())
        {
            const double value = std::prev(it)->bpm;
            if (std::isfinite(value))
            {
                if (!chartBegun) { chartPath.moveTo(x(value), y); chartBegun = true; }
                else { chartPath.lineTo(x(value), y - 1); chartPath.lineTo(x(value), y); }
            }
        }
        if (!m_interpolation.valid()) continue;
        const double relative = beat - m_options.start.position().toDouble();
        if (relative < 0 || relative > m_options.length.beats()) continue;
        const double bpm = m_options.startBpm + (m_options.endBpm - m_options.startBpm) * relative / m_options.length.beats();
        if (!curveBegun) { curvePath.moveTo(x(bpm), y); curveBegun = true; } else curvePath.lineTo(x(bpm), y);
        const auto node = std::upper_bound(m_interpolation.nodes.begin(), m_interpolation.nodes.end(), relative,
            [](double b, const auto &n) { return b < n.relativeBeat; });
        const int i = qMax(0, int(node - m_interpolation.nodes.begin()) - 1);
        const double value = m_stored.value(i, m_interpolation.nodes[size_t(i)].bpm);
        if (!previewBegun) { previewPath.moveTo(x(value), y); previewBegun = true; }
        else { previewPath.lineTo(x(value), y - 1); previewPath.lineTo(x(value), y); }
    }
    p.setPen(QPen(actual, 2)); p.drawPath(chartPath);
    p.setPen(QPen(stored, 1.5)); p.drawPath(previewPath);
    if (m_interpolation.valid())
    {
        p.setBrush(stored);
        double previousY = -100;
        for (size_t i = 0; i < m_interpolation.nodes.size(); ++i)
        {
            const auto &node = m_interpolation.nodes[i];
            const double y = yAtBeat(node.beat.position().toDouble());
            if (y < topMargin || y > height() || std::abs(y - previousY) < 3) continue;
            p.drawEllipse(QPointF(x(m_stored.value(int(i), node.bpm)), y), 2, 2);
            previousY = y;
        }
    }
    p.setPen(QPen(ideal, 2, Qt::DashLine)); p.drawPath(curvePath);
    const auto first = std::lower_bound(m_entries.cbegin(), m_entries.cend(), low,
        [](const auto &e, double b) { return e.position().toDouble() < b; });
    const auto last = std::upper_bound(first, m_entries.cend(), high,
        [](double b, const auto &e) { return b < e.position().toDouble(); });
    if (last - first <= height())
        for (auto it = first; it != last; ++it)
        { p.setBrush(actual); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(x(it->bpm), yAtBeat(it->position().toDouble())), 3, 3); }
    if (m_measureStart)
    {
        p.setPen(QPen(measured, 1.5));
        const double start = yAtTime(*m_measureStart);
        p.drawLine(QPointF(75, start), QPointF(width(), start));
        p.drawText(QPointF(85, start - 4), tr("Start"));
        if (m_measureEnd)
        {
            const double end = yAtTime(*m_measureEnd);
            p.drawLine(QPointF(75, end), QPointF(width(), end));
            p.drawText(QPointF(85, end - 4), tr("End"));
            if (m_measureBpm > 0) p.drawLine(QPointF(x(m_measureBpm), start), QPointF(x(m_measureBpm), end));
            if (m_measureBpm > 0)
            {
                const double interval = 60000. / m_measureBpm;
                const double lo = qMax(*m_measureStart, qMin(timeAtY(topMargin), timeAtY(height())));
                const double hi = qMin(*m_measureEnd, qMax(timeAtY(topMargin), timeAtY(height())));
                const double stride = qMax(1., std::ceil((hi - lo) / interval / 64.));
                for (double t = *m_measureStart + std::ceil((lo - *m_measureStart) / interval) * interval;
                     t <= hi; t += interval * stride)
                    p.drawLine(QPointF(75, yAtTime(t)), QPointF(width(), yAtTime(t)));
            }
        }
    }
    if (m_selected >= 0 && m_selected < m_entries.size())
    {
        const auto &point = m_entries[m_selected];
        p.setPen(QPen(palette().highlight().color(), 2)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(x(point.bpm), yAtBeat(point.position().toDouble())), 6, 6);
    }
    p.setPen(QPen(QColor(255, 100, 100), 1));
    const double playY = yAtBeat(beatAtTime(m_main->currentPlayTime()));
    p.drawLine(QPointF(75, playY), QPointF(width(), playY));
    p.restore();
}
void BpmCurveCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->position().y() < topMargin) return;
    setFocus();
    if (e->button() == Qt::MiddleButton)
    { emit seekRequested(qMax(0., timeAtY(e->position().y()))); return; }
    if (e->button() != Qt::LeftButton) return;
    const double y = e->position().y();
    if (m_interpolationPicking) { pickStart(y); return; }
    if (m_measurePicking)
    {
        if (m_measureStart && std::abs(yAtTime(*m_measureStart) - y) <= 8) m_dragging = 1;
        else if (m_measureEnd && std::abs(yAtTime(*m_measureEnd) - y) <= 8) m_dragging = 2;
        else if (!m_measureStart || m_measureEnd) { pickStart(y); m_dragging = 1; }
        else { emit measurementEndRequested(qMax(0., timeAtY(y))); m_dragging = 2; }
        return;
    }
    const int index = nearestTiming(y);
    selectTiming(index);
    if (index >= 0) emit timingSelected(index);
    emit seekRequested(qMax(0., index >= 0 ? timeAtBeat(m_entries[index].position().toDouble()) : timeAtY(y)));
}
void BpmCurveCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton)) return;
    if (m_dragging == 1) pickStart(e->position().y());
    else if (m_dragging == 2) emit measurementEndRequested(qMax(0., timeAtY(e->position().y())));
}
void BpmCurveCanvas::mouseReleaseEvent(QMouseEvent *) { m_dragging = 0; }
void BpmCurveCanvas::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.;
    if (e->modifiers() & Qt::ControlModifier)
        m_main->setTimeScale(qBound(.1, m_main->timeScale() * std::pow(1.15, steps), 30.));
    else
    {
        const double beat = qMax(0., beatAtTime(m_main->currentPlayTime()) + steps * m_main->visibleBeatRange() / 12.);
        m_main->setScrollPos(timeAtBeat(beat));
    }
    update(); e->accept();
}
void BpmCurveCanvas::showEvent(QShowEvent *event) { QWidget::showEvent(event); update(); }

#include "AnalysisCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMenu>
#include <algorithm>
#include <cmath>
namespace
{
QColor spectrumColor(float db)
{
    const double v = std::clamp((db + 90.0) / 90.0, 0.0, 1.0);
    return QColor::fromRgbF(std::clamp(2.4 * v - 1.0, 0.0, 1.0), std::clamp(1.9 * v - 0.3, 0.0, 1.0),
                            std::clamp(3 * v, 0.04, 1.0) * (1 - 0.65 * v));
}
} // namespace
AnalysisCanvas::AnalysisCanvas(ChartController *chart, SelectionController *selection, ChartCanvas *main,
                               QWidget *parent)
    : QWidget(parent), m_chart(chart), m_selection(selection), m_main(main)
{
    setObjectName("analysis.canvas");
    setMinimumWidth(180);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    connect(chart, &ChartController::chartChanged, this, [this] {
        rebuildChartCache();
        update();
    });
    connect(chart, &ChartController::chartLoaded, this, &AnalysisCanvas::cancelGesture);
    connect(selection, &SelectionController::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(main, &ChartCanvas::timeScaleChanged, this, [this] {
        if (m_syncView)
        {
            update();
            emit viewportChanged();
        }
    });
    connect(main, &ChartCanvas::verticalFlipChanged, this, [this] {
        alignViewport();
        update();
        emit viewportChanged();
    });
    rebuildChartCache();
}
int AnalysisCanvas::spectrumWidth() const
{
    return width() - qBound(42, m_noteLaneWidth, qMax(42, width() - 80));
}
double AnalysisCanvas::timeAtBeat(double beat) const
{
    if (m_bpm.isEmpty())
        return 0;
    auto it = std::upper_bound(m_bpm.cbegin(), m_bpm.cend(), beat,
                               [](double b, const auto &e) { return b < e.beatPos; });
    if (it != m_bpm.cbegin())
        --it;
    return it->bpm > 0 ? it->accumulatedMs + (beat - it->beatPos) * 60000 / it->bpm : it->accumulatedMs;
}
double AnalysisCanvas::beatAtTime(double ms) const
{
    if (m_bpm.isEmpty())
        return 0;
    auto it = std::upper_bound(m_bpm.cbegin(), m_bpm.cend(), ms,
                               [](double t, const auto &e) { return t < e.accumulatedMs; });
    if (it != m_bpm.cbegin())
        --it;
    return it->beatPos + (ms - it->accumulatedMs) * it->bpm / 60000;
}
double AnalysisCanvas::timeAtY(double y) const
{
    const double h = qMax(1, height() - headerHeight);
    double ratio = (y - headerHeight) / h;
    if (m_syncView)
        return timeAtBeat(m_main->chartYToBeat(ratio * m_main->height()));
    if (m_main->isVerticalFlip())
        ratio = 1 - ratio;
    return m_viewStart + ratio * h * m_msPerPixel;
}
double AnalysisCanvas::yAtTime(double ms) const
{
    const double h = qMax(1, height() - headerHeight);
    double ratio;
    if (m_syncView)
        ratio = m_main->chartBeatToY(beatAtTime(ms)) / qMax(1, m_main->height());
    else
    {
        ratio = (ms - m_viewStart) / (h * m_msPerPixel);
        if (m_main->isVerticalFlip())
            ratio = 1 - ratio;
    }
    return headerHeight + ratio * h;
}
void AnalysisCanvas::alignViewport()
{
    const double span = qMax(1, height() - headerHeight) * m_msPerPixel;
    m_viewStart = qMax(0.0, m_current - span * (m_main->isVerticalFlip() ? 0.2 : 0.8));
}
void AnalysisCanvas::setCurrentTime(double ms)
{
    if (!std::isfinite(ms))
        return;
    m_current = qMax(0.0, ms);
    alignViewport();
    update();
    emit viewportChanged();
}
void AnalysisCanvas::setMillisecondsPerPixel(double value)
{
    if (!std::isfinite(value))
        return;
    m_msPerPixel = qBound(0.1, value, 100.0);
    alignViewport();
    update();
    emit viewportChanged();
}
void AnalysisCanvas::setNoteLaneWidth(int width)
{
    m_noteLaneWidth = qBound(42, width, 4096);
    update();
}
void AnalysisCanvas::setViewSynchronized(bool enabled)
{
    m_syncView = enabled;
    alignViewport();
    update();
    emit viewportChanged();
}
void AnalysisCanvas::setRainMode(bool enabled)
{
    cancelGesture();
    m_rainMode = enabled;
    update();
}
void AnalysisCanvas::setRange(double a, double b, bool visible)
{
    m_rangeStart = a;
    m_rangeEnd = b;
    m_rangeVisible = visible;
    update();
}
void AnalysisCanvas::setLoop(double a, double b, bool enabled)
{
    m_loopStart = a;
    m_loopEnd = b;
    m_loopEnabled = enabled;
    update();
}
void AnalysisCanvas::setStatus(const QString &text)
{
    m_status = text;
    update();
}
void AnalysisCanvas::setTimingPreview(double bpm, double pulseMs, double startMs, double endMs)
{
    if (!std::isfinite(bpm) || bpm <= 0 || bpm > 10000 || !std::isfinite(pulseMs) || !std::isfinite(startMs)
        || !std::isfinite(endMs) || startMs < 0 || endMs <= startMs)
    {
        clearTimingPreview();
        return;
    }
    m_timingPreview = TimingPreview{bpm, pulseMs, startMs, endMs};
    update();
}
void AnalysisCanvas::clearTimingPreview()
{
    m_timingPreview.reset();
    update();
}
void AnalysisCanvas::setMeasurementPicking(bool enabled)
{
    cancelGesture();
    m_measurePicking = enabled;
    update();
}
void AnalysisCanvas::setMeasurementOverlay(std::optional<double> start, std::optional<double> end,
                                         double bpm, const QString &label)
{
    m_measureStart = start;
    m_measureEnd = end;
    m_measureBpm = bpm;
    m_measureStartLabel = label;
    update();
}
void AnalysisCanvas::clearSpectrum()
{
    m_spectrum = {};
    m_leftImage = {};
    m_rightImage = {};
    update();
}
void AnalysisCanvas::setSpectrum(analysis::StereoSpectrum data)
{
    m_spectrum = std::move(data);
    const size_t bins = m_spectrum.frequencies.size(), frames = m_spectrum.frames.size();
    if (!m_spectrum.valid() || !bins || frames > 12000 || bins > 4096 ||
        !std::isfinite(m_spectrum.hopSeconds) || m_spectrum.hopSeconds <= 0 ||
        !std::isfinite(m_spectrum.startSeconds) || m_spectrum.startSeconds < 0 ||
        m_spectrum.leftDb.size() != frames * bins || m_spectrum.rightDb.size() != frames * bins)
    {
        m_status = tr("Invalid spectrum result");
        m_spectrum = {};
        m_leftImage = {};
        m_rightImage = {};
        update();
        return;
    }
    m_leftImage = QImage(int(bins), int(frames), QImage::Format_RGB32);
    m_rightImage = QImage(int(bins), int(frames), QImage::Format_RGB32);
    for (size_t y = 0; y < frames; ++y)
    {
        auto *l = reinterpret_cast<QRgb *>(m_leftImage.scanLine(int(y))),
             *r = reinterpret_cast<QRgb *>(m_rightImage.scanLine(int(y)));
        for (size_t x = 0; x < bins; ++x)
        {
            l[x] = spectrumColor(m_spectrum.leftDb[y * bins + x]).rgb();
            r[x] = spectrumColor(m_spectrum.rightDb[y * bins + x]).rgb();
        }
    }
    m_status.clear();
    update();
}
void AnalysisCanvas::rebuildChartCache()
{
    const auto *chart = m_chart->chart();
    m_bpm = MathUtils::buildBpmTimeCache(chart->bpmList(), chart->meta().offset);
    const auto &notes = chart->notes();
    m_starts.resize(notes.size());
    m_ends.resize(notes.size());
    QVector<int> ids;
    ids.reserve(notes.size());
    for (int i = 0; i < notes.size(); ++i)
    {
        m_starts[i] = timeAtBeat(notes[i].getStartBeat());
        m_ends[i] = timeAtBeat(notes[i].getEndBeat());
        if (notes[i].isNormal() || notes[i].isRainNote())
            ids.append(i);
    }
    m_noteIndex.build(ids, m_starts, m_ends);
    m_tailOriginal.reset();
    m_tailPreview.reset();
}
void AnalysisCanvas::drawSpectrum(QPainter &p)
{
    const int sw = spectrumWidth(), gutter = 16, chWidth = (sw - 2 * gutter - 8) / 2;
    const int x[2] = {gutter, sw / 2 + 4};
    if (m_leftImage.isNull())
    {
        p.setPen(QColor(145, 161, 181));
        p.drawText(QRect(12, 45, sw - 24, 140), Qt::TextWordWrap,
                   m_status.isEmpty() ? tr("Load chart audio to inspect stereo spectrum") : m_status);
        return;
    }
    for (int y = headerHeight; y < height(); ++y)
    {
        const int frame =
            int(std::floor((timeAtY(y) / 1000 - m_spectrum.startSeconds) / m_spectrum.hopSeconds));
        if (frame < 0 || frame >= m_leftImage.height())
            continue;
        p.drawImage(QRect(x[0], y, chWidth, 1), m_leftImage, QRect(0, frame, m_leftImage.width(), 1));
        p.drawImage(QRect(x[1], y, chWidth, 1), m_rightImage, QRect(0, frame, m_rightImage.width(), 1));
        const auto &f = m_spectrum.frames[size_t(frame)];
        p.setPen(QColor(70, 187, 171, 190));
        p.drawLine(1, y, qRound(qMin(1.f, f.leftPeak) * (gutter - 2)), y);
        p.drawLine(sw / 2 - gutter, y, sw / 2 - gutter + qRound(qMin(1.f, f.rightPeak) * (gutter - 2)), y);
        p.setPen(QColor(204, 232, 159, 220));
        p.drawPoint(qRound(qMin(1.f, f.leftRms) * (gutter - 2)), y);
        p.drawPoint(sw / 2 - gutter + qRound(qMin(1.f, f.rightRms) * (gutter - 2)), y);
    }
    if (chWidth > 110 && !m_spectrum.frequencies.empty())
    {
        const double low = m_spectrum.frequencies.front(), high = m_spectrum.frequencies.back();
        for (int ch = 0; ch < 2; ++ch)
        {
            p.fillRect(QRect(x[ch], height() - 17, chWidth, 17), QColor(19, 24, 33, 215));
            p.setPen(QColor(164, 180, 198));
            for (double hz : {100., 1000., 10000.})
            {
                if (hz < low || hz > high)
                    continue;
                const QString label = hz < 1000 ? tr("100 Hz") : hz < 10000 ? tr("1 kHz") : tr("10 kHz");
                const int tick = x[ch] + qRound(std::log(hz / low) / std::log(high / low) * chWidth);
                const int labelWidth = p.fontMetrics().horizontalAdvance(label);
                p.drawText(qBound(x[ch], tick - labelWidth / 2, x[ch] + chWidth - labelWidth), height() - 4,
                           label);
            }
        }
    }
}
void AnalysisCanvas::drawTiming(QPainter &p)
{
    const double a = beatAtTime(timeAtY(headerHeight)), b = beatAtTime(timeAtY(height()));
    const double first = qMax(0.0, qMin(a, b)), last = qMax(a, b);
    if (!std::isfinite(first) || !std::isfinite(last) || last > 1e9)
        return;
    double step = 1.0 / qMax(1, m_main->timeDivision());
    if ((last - first) / step > 2000)
        step = std::ceil((last - first) / 2000);
    for (double beat = std::ceil(first / step) * step; beat <= last; beat += step)
    {
        const double y = yAtTime(timeAtBeat(beat));
        const bool whole = qAbs(beat - std::round(beat)) < 1e-7;
        const bool measure = whole && qRound64(beat) % 4 == 0;
        p.setPen(QPen(measure ? QColor(151, 173, 193, 130)
                      : whole ? QColor(112, 134, 154, 100)
                              : QColor(78, 98, 119, 60),
                      measure ? 1.4 : 1));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        if (measure)
        {
            p.setPen(QColor(174, 193, 209));
            p.drawText(spectrumWidth() + 5, int(y) - 3, QString::number(qRound64(beat)));
        }
    }
    for (const auto &e : m_bpm)
    {
        const double y = yAtTime(e.accumulatedMs);
        if (y < headerHeight || y > height())
            continue;
        p.setPen(QPen(QColor(244, 194, 108), 1, Qt::DashLine));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        const QString label = QStringLiteral("[%1]").arg(e.bpm, 0, 'f', 2);
        const QRect r(5, qRound(y) - 18, p.fontMetrics().horizontalAdvance(label) + 10, 18);
        p.fillRect(r, QColor(34, 38, 48, 220));
        p.drawText(r, Qt::AlignCenter, label);
    }
}
void AnalysisCanvas::drawTimingPreview(QPainter &p)
{
    if (!m_timingPreview)
        return;
    const auto &grid = *m_timingPreview;
    const double a = timeAtY(headerHeight), b = timeAtY(height());
    const double low = qMax(grid.startMs, qMin(a, b)), high = qMin(grid.endMs, qMax(a, b));
    if (high <= low)
        return;
    const double period = 60000 / grid.bpm;
    // Thin dense references without shifting phase or changing placement snap.
    const int limit = qBound(1, (height() - headerHeight) / 6, 1024);
    const double stride = qMax(1.0, std::ceil((high - low) / (period * limit)));
    const double first = std::ceil((low - grid.pulseMs) / (period * stride));
    p.setPen(QPen(QColor(85, 211, 239, 190), 1, Qt::DashDotLine));
    for (int i = 0; i <= limit; ++i)
    {
        const double time = grid.pulseMs + (first + i) * stride * period;
        if (!std::isfinite(time) || time > high)
            break;
        const double y = yAtTime(time);
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }
    p.setPen(QPen(QColor(85, 211, 239), 1, Qt::DotLine));
    for (double time : {grid.startMs, grid.endMs})
        if (time >= qMin(a, b) && time <= qMax(a, b))
            p.drawLine(QPointF(0, yAtTime(time)), QPointF(width(), yAtTime(time)));
}
void AnalysisCanvas::drawMeasurement(QPainter &p)
{
    if (!m_measureStart)
        return;
    const int sw = spectrumWidth();
    if (m_measureEnd && m_measureBpm > 0)
    {
        const double a = timeAtY(headerHeight), b = timeAtY(height());
        const double low = qMax(*m_measureStart, qMin(a, b)), high = qMin(*m_measureEnd, qMax(a, b));
        const double period = 60000 / m_measureBpm;
        const int limit = qBound(1, (height() - headerHeight) / 6, 1024);
        const double stride = qMax(1.0, std::ceil((high - low) / (period * limit)));
        const double first = std::ceil((low - *m_measureStart) / (period * stride));
        p.setPen(QPen(QColor(182, 151, 255, 210), 1, Qt::DashDotLine));
        for (int i = 0; i <= limit && high > low; ++i)
        {
            const double time = *m_measureStart + (first + i) * stride * period;
            if (time > high)
                break;
            p.drawLine(QPointF(0, yAtTime(time)), QPointF(sw, yAtTime(time)));
        }
    }
    auto marker = [&](double ms, bool start) {
        const double y = yAtTime(ms);
        if (y < headerHeight || y > height())
            return;
        const QColor color = start ? QColor(255, 205, 96) : QColor(221, 172, 255);
        p.setPen(QPen(color, 2));
        p.drawLine(QPointF(0, y), QPointF(sw, y));
        const QString label = start ? m_measureStartLabel : tr("End %1 ms").arg(ms, 0, 'f', 3);
        const int textWidth = qMin(sw - 12, p.fontMetrics().horizontalAdvance(label) + 12);
        const QRect box(start ? 4 : sw - textWidth - 4, qRound(y) - (start ? 22 : -3), textWidth, 20);
        p.fillRect(box, QColor(24, 27, 39, 235));
        p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter,
                   p.fontMetrics().elidedText(label, Qt::ElideRight, textWidth - 8));
        p.setBrush(color);
        p.drawEllipse(QPointF(start ? 7 : sw - 7, y), 4, 4);
    };
    marker(*m_measureStart, true);
    if (m_measureEnd)
        marker(*m_measureEnd, false);
}
void AnalysisCanvas::drawNotes(QPainter &p)
{
    const double low = qMin(timeAtY(headerHeight - 12), timeAtY(height() + 12)),
                 high = qMax(timeAtY(headerHeight - 12), timeAtY(height() + 12));
    const auto visible = m_noteIndex.overlapping(low, high);
    const auto &notes = m_chart->chart()->notes();
    const auto selected = m_selection->selectedIndices();
    const double x = (spectrumWidth() + width()) / 2.0;
    // Paint Rain first so ordinary Notes remain visible inside its rectangle.
    for (bool rainPass : {true, false})
        for (auto i = visible.begin; i < visible.end; ++i)
        {
            const auto e = m_noteIndex.entryAt(i);
            if (e.end < low || e.start > high)
                continue;
            const Note &n = m_tailPreview && m_tailOriginal && notes[e.index].id == m_tailOriginal->id ? *m_tailPreview
                                                                                                       : notes[e.index];
            if (n.isRainNote() != rainPass)
                continue;
            const double y = yAtTime(timeAtBeat(n.getStartBeat()));
            const QColor color = selected.contains(e.index) ? QColor(255, 217, 113)
                                 : n.isRainNote()           ? QColor(105, 169, 255)
                                                            : QColor(170, 216, 236);
            p.setPen(QPen(color, 2));
            p.setBrush(color);
            if (n.isRainNote())
            {
                const double tail = yAtTime(timeAtBeat(n.getEndBeat()));
                p.setBrush(QColor(color.red(), color.green(), color.blue(), 75));
                p.drawRect(QRectF(spectrumWidth() + 6, qMin(y, tail), width() - spectrumWidth() - 12,
                                  qMax(1.0, qAbs(tail - y))));
            }
            else
                p.drawRoundedRect(QRectF(x - 14, y - 4, 28, 8), 3, 3);
        }
    if (m_rainAnchor)
    {
        const double y = yAtTime(timeAtBeat(m_rainAnchor->getStartBeat()));
        p.setPen(QPen(QColor(105, 169, 255), 1, Qt::DashLine));
        p.drawLine(QPointF(spectrumWidth(), y), QPointF(width(), y));
    }
}
void AnalysisCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(19, 24, 33));
    p.setClipRect(QRect(0, headerHeight, width(), height() - headerHeight));
    drawSpectrum(p);
    auto shade = [&](double a, double b, QColor color) {
        a = yAtTime(a);
        b = yAtTime(b);
        p.fillRect(QRectF(0, qMin(a, b), width(), qAbs(a - b)), color);
    };
    if (m_rangeVisible)
        shade(timeAtBeat(m_rangeStart), timeAtBeat(m_rangeEnd), QColor(226, 193, 86, 25));
    if (m_loopEnabled)
        shade(m_loopStart, m_loopEnd, QColor(86, 217, 172, 23));
    drawTiming(p);
    drawTimingPreview(p);
    drawNotes(p);
    p.setPen(QPen(QColor(255, 119, 113), 1.5));
    p.drawLine(QPointF(0, yAtTime(m_current)), QPointF(width(), yAtTime(m_current)));
    drawMeasurement(p);
    if (!m_status.isEmpty() && !m_leftImage.isNull())
    {
        const QRect status(8, headerHeight + 6, spectrumWidth() - 16, 42);
        p.fillRect(status, QColor(19, 24, 33, 225));
        p.setPen(QColor(190, 209, 228));
        p.drawText(status.adjusted(4, 2, -4, -2), Qt::TextWordWrap, m_status);
    }
    p.setClipping(false);
    p.fillRect(QRect(0, 0, width(), headerHeight), QColor(30, 38, 50));
    p.setPen(QColor(201, 215, 231));
    QString spectrumTitle = m_spectrum.sourceChannels == 1 ? tr("Spectrum · Mono → L / R") : tr("Spectrum · L / R");
    if (m_timingPreview)
    {
        spectrumTitle += tr(" · Preview %1 BPM").arg(m_timingPreview->bpm, 0, 'f', 3);
        p.setPen(QColor(85, 211, 239));
    }
    if (m_measurePicking)
        spectrumTitle = m_measureStart ? tr("Measure · End / drag markers") : tr("Measure · pick Start (snap)");
    p.drawText(QRect(2, 0, spectrumWidth() - 4, headerHeight), Qt::AlignCenter,
               p.fontMetrics().elidedText(spectrumTitle, Qt::ElideRight, spectrumWidth() - 8));
    p.setPen(QColor(201, 215, 231));
    const QRect tool(spectrumWidth() + 2, 2, width() - spectrumWidth() - 4, headerHeight - 4);
    p.fillRect(tool, QColor(65, 87, 115));
    p.drawText(tool.adjusted(2, 0, -12, 0), Qt::AlignCenter, m_rainMode ? tr("Rain") : tr("Note"));
    const double arrowX = tool.right() - 6;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(201, 215, 231));
    p.drawPolygon(QPolygonF{QPointF(arrowX - 3, 12), QPointF(arrowX + 3, 12), QPointF(arrowX, 16)});
    p.setPen(QPen(QColor(92, 112, 137), 3));
    p.drawLine(spectrumWidth(), 0, spectrumWidth(), height());
}
Note AnalysisCanvas::snappedNoteAtY(double y) const
{
    int b, n, d;
    MathUtils::floatToBeat(qMax(0.0, beatAtTime(timeAtY(y))), b, n, d);
    return MathUtils::snapNoteToTimeWithBoundary(Note(b, n, d, 256), m_main->timeDivision());
}
int AnalysisCanvas::hitNote(double y) const
{
    const double a = timeAtY(y - 8), b = timeAtY(y + 8), low = qMin(a, b), high = qMax(a, b);
    const auto visible = m_noteIndex.overlapping(low, high);
    int best = -1;
    double distance = 9;
    for (auto i = visible.begin; i < visible.end; ++i)
    {
        const auto e = m_noteIndex.entryAt(i);
        if (e.end < low || e.start > high)
            continue;
        const double startY = yAtTime(e.start), endY = yAtTime(e.end);
        double dist = qMin(qAbs(y - startY), qAbs(y - endY));
        // A Rain body must not hide an ordinary Note at the same time. In Note
        // mode its interior also remains available for placing ordinary Notes.
        if (m_rainMode && y >= qMin(startY, endY) && y <= qMax(startY, endY))
            dist = qMin(dist, 8.0);
        if (dist < distance)
        {
            best = e.index;
            distance = dist;
        }
    }
    return best;
}
void AnalysisCanvas::cancelGesture()
{
    m_rainAnchor.reset();
    m_tailOriginal.reset();
    m_tailPreview.reset();
    m_resizeDivider = false;
    m_selectingRange = false;
    m_measureDragging = 0;
    update();
}
void AnalysisCanvas::pickMeasurementStart(double y)
{
    const auto &timing = m_chart->chart()->bpmList();
    // A clicked existing timing line retains even a non-canonical triplet.
    const BpmEntry *nearest = nullptr;
    double distance = 5;
    for (const auto &point : timing)
    {
        const double delta = qAbs(yAtTime(timeAtBeat(point.position().toDouble())) - y);
        if (delta < distance)
        {
            nearest = &point;
            distance = delta;
        }
    }
    if (nearest)
    {
        emit measurementStartRequested(nearest->beatNum, nearest->numerator, nearest->denominator);
        return;
    }
    int b, n, d;
    if (!MathUtils::quantizeBeatToDivision(qMax(0.0, beatAtTime(timeAtY(y))), m_main->timeDivision(), b, n, d))
        return;
    const BeatPosition position(b, n, d);
    for (const auto &point : timing)
        if (point.position() == position)
        {
            emit measurementStartRequested(point.beatNum, point.numerator, point.denominator);
            return;
        }
    emit measurementStartRequested(b, n, d);
}
void AnalysisCanvas::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const auto pos = e->position();
    if (e->button() == Qt::LeftButton && qAbs(pos.x() - spectrumWidth()) <= 5)
    {
        m_resizeDivider = true;
        return;
    }
    if (pos.y() < headerHeight)
    {
        if (e->button() == Qt::LeftButton && pos.x() > spectrumWidth() + 5)
        {
            auto *menu = new QMenu(this);
            menu->setObjectName("analysis.noteTools");
            menu->setAttribute(Qt::WA_DeleteOnClose);
            for (bool rain : {false, true})
            {
                auto *action = menu->addAction(rain ? tr("Rain") : tr("Note"));
                action->setCheckable(true);
                action->setChecked(rain == m_rainMode);
                connect(action, &QAction::triggered, this, [this, rain] {
                    setRainMode(rain);
                });
            }
            menu->popup(mapToGlobal(QPoint(spectrumWidth(), headerHeight)));
        }
        return;
    }
    if (m_measurePicking && pos.x() < spectrumWidth() - 5 && e->button() == Qt::LeftButton
        && !(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
    {
        const double startDistance = m_measureStart ? qAbs(yAtTime(*m_measureStart) - pos.y()) : 1e12;
        const double endDistance = m_measureEnd ? qAbs(yAtTime(*m_measureEnd) - pos.y()) : 1e12;
        m_measureDragging = !m_measureStart || (startDistance <= 8 && startDistance <= endDistance) ? 1 : 2;
        if (m_measureDragging == 1)
            pickMeasurementStart(pos.y());
        else
            emit measurementEndRequested(qMax(0.0, timeAtY(pos.y())));
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier))
    {
        m_selectingRange = true;
        m_rangeAnchor = snappedNoteAtY(pos.y()).getStartBeat();
        setRange(m_rangeAnchor, m_rangeAnchor);
        return;
    }
    if (pos.x() < spectrumWidth() || e->button() == Qt::MiddleButton)
    {
        if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton)
            emit seekRequested(qMax(0.0, timeAtY(pos.y())));
        return;
    }
    const int hit = hitNote(pos.y());
    if (e->button() == Qt::RightButton)
    {
        if (m_rainAnchor)
        {
            m_rainAnchor.reset();
            update();
            return;
        }
        if (hit >= 0)
            m_chart->removeNote(m_chart->chart()->notes()[hit]);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (hit >= 0)
    {
        const auto note = m_chart->chart()->notes()[hit];
        if (e->modifiers() & Qt::ControlModifier)
        {
            if (m_selection->selectedIndices().contains(hit))
                m_selection->removeFromSelection(hit);
            else
                m_selection->addToSelection(hit);
        }
        else
            m_selection->select(hit);
        if (note.isRainNote() && qAbs(yAtTime(m_ends[hit]) - pos.y()) <= 7)
        {
            m_tailOriginal = note;
            m_tailPreview = note;
        }
        return;
    }
    const Note note = snappedNoteAtY(pos.y());
    if (!m_rainMode)
    {
        m_chart->addNote(note);
        return;
    }
    if (!m_rainAnchor)
    {
        m_rainAnchor = note;
        update();
        return;
    }
    const Note anchor = *m_rainAnchor;
    m_rainAnchor.reset();
    Note rain(anchor.beatNum, anchor.numerator, anchor.denominator, note.beatNum, note.numerator,
              note.denominator, anchor.x);
    if (rain.isValidRain())
        m_chart->addNote(rain);
    else
        emit statusMessage(tr("Rain end must be later than its start"));
    update();
}
void AnalysisCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_measureDragging)
    {
        if (m_measureDragging == 1)
            pickMeasurementStart(e->position().y());
        else
            emit measurementEndRequested(qMax(0.0, timeAtY(e->position().y())));
        return;
    }
    if (m_resizeDivider)
    {
        setNoteLaneWidth(qBound(42, width() - qRound(e->position().x()), qMax(42, width() - 80)));
        return;
    }
    if (m_selectingRange)
    {
        const double b = snappedNoteAtY(e->position().y()).getStartBeat();
        setRange(qMin(m_rangeAnchor, b), qMax(m_rangeAnchor, b));
        return;
    }
    if (m_tailOriginal)
    {
        const Note tail = snappedNoteAtY(e->position().y());
        Note n = *m_tailOriginal;
        n.endBeatNum = tail.beatNum;
        n.endNumerator = tail.numerator;
        n.endDenominator = tail.denominator;
        if (n.isValidRain())
        {
            m_tailPreview = n;
            update();
        }
        return;
    }
    setCursor(qAbs(e->position().x() - spectrumWidth()) <= 5 ? Qt::SplitHCursor : Qt::ArrowCursor);
}
void AnalysisCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_resizeDivider = false;
    m_measureDragging = 0;
    if (m_selectingRange)
    {
        m_selectingRange = false;
        emit rangeRequested(m_rangeStart, m_rangeEnd);
    }
    if (m_tailOriginal && m_tailPreview)
    {
        const auto original = *m_tailOriginal, changed = *m_tailPreview;
        m_tailOriginal.reset();
        m_tailPreview.reset();
        if (original != changed)
            m_chart->moveNote(original, changed);
    }
}
void AnalysisCanvas::wheelEvent(QWheelEvent *e)
{
    const double d = e->angleDelta().y() ? e->angleDelta().y() / 120.0 : e->pixelDelta().y() / 40.0;
    if (e->modifiers() & Qt::ControlModifier)
    {
        if (m_syncView)
            m_main->setTimeScale(m_main->timeScale() * std::pow(1.15, d));
        else
            setMillisecondsPerPixel(m_msPerPixel * std::pow(1.15, -d));
    }
    else
        emit seekRequested(qMax(0.0, m_current + d * m_msPerPixel * 45));
    e->accept();
}
void AnalysisCanvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        cancelGesture();
        if (m_measurePicking)
            emit measurementCancelRequested();
        e->accept();
    }
    else
        QWidget::keyPressEvent(e);
}
bool AnalysisCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress && m_measurePicking)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier)
        {
            emit measurementDetailsRequested();
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}
void AnalysisCanvas::resizeEvent(QResizeEvent *)
{
    alignViewport();
    emit viewportChanged();
}

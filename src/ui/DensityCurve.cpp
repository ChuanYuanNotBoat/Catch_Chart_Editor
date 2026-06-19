#include "DensityCurve.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/Chart.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "utils/MathUtils.h"
#include "utils/Settings.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

DensityCurve::DensityCurve(QWidget *parent)
    : QWidget(parent),
      m_chartController(nullptr),
      m_playbackController(nullptr),
      m_canvas(nullptr),
      m_chart(nullptr),
      m_currentTimeMs(0),
      m_durationMs(60000.0),
      m_tipTimeMs(0),
      m_dragging(false),
      m_showTip(false)
{
    setFixedWidth(58);
    setMouseTracking(true);
    m_densityData.fill(0, kBinCount);
}

void DensityCurve::setChartController(ChartController *controller)
{
    if (m_chartController)
    {
        disconnect(m_chartController, &ChartController::chartChanged, this, nullptr);
        disconnect(m_chartController, &ChartController::chartLoaded, this, nullptr);
    }
    m_chartController = controller;
    m_chart = (m_chartController ? m_chartController->chart() : nullptr);
    if (m_chartController)
    {
        connect(m_chartController, &ChartController::chartChanged, this, &DensityCurve::refreshFromChart, Qt::UniqueConnection);
        connect(m_chartController, &ChartController::chartLoaded, this, &DensityCurve::refreshFromChart, Qt::UniqueConnection);
    }
    refreshFromChart();
}

void DensityCurve::setPlaybackController(PlaybackController *controller)
{
    if (m_playbackController)
    {
        disconnect(m_playbackController, &PlaybackController::positionChanged, this, nullptr);
        if (m_playbackController->audioPlayer())
            disconnect(m_playbackController->audioPlayer(), &AudioPlayer::durationChanged, this, nullptr);
    }
    m_playbackController = controller;
    if (m_playbackController)
    {
        connect(m_playbackController, &PlaybackController::positionChanged, this, [this](double timeMs)
                {
            if (!m_dragging)
                syncCurrentTime(timeMs); });
        if (m_playbackController->audioPlayer())
        {
            connect(m_playbackController->audioPlayer(), &AudioPlayer::durationChanged, this, [this](qint64)
                    {
                syncDuration();
                computeDensity();
                update(); });
        }
    }
    syncDuration();
    computeDensity();
    update();
}

void DensityCurve::setCanvas(ChartCanvas *canvas)
{
    if (m_canvas)
        disconnect(m_canvas, &ChartCanvas::scrollPositionChanged, this, nullptr);
    m_canvas = canvas;
    if (m_canvas)
    {
        connect(m_canvas, &ChartCanvas::scrollPositionChanged, this, [this](double beat)
                {
            if (m_dragging)
                return;
            if (m_playbackController && m_playbackController->state() == PlaybackController::Playing)
                return;
            updateFromCanvasBeat(beat); });
    }
}

QString DensityCurve::formatTimeMs(double timeMs)
{
    const qint64 clamped = qMax<qint64>(0, static_cast<qint64>(qRound64(timeMs)));
    const int ms = static_cast<int>(clamped % 1000);
    const int sec = static_cast<int>((clamped / 1000) % 60);
    const int min = static_cast<int>(clamped / 60000);
    return QStringLiteral("%1:%2:%3")
        .arg(min, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

void DensityCurve::syncDuration()
{
    double duration = 0.0;
    if (m_playbackController && m_playbackController->audioPlayer())
        duration = static_cast<double>(m_playbackController->audioPlayer()->duration());

    if (duration <= 0.0 && m_chart)
    {
        double maxNoteMs = 0.0;
        const auto &notes = m_chart->notes();
        for (const Note &note : notes)
        {
            const double t = MathUtils::beatToMs(
                note.beatNum, note.numerator, note.denominator, m_chart->bpmList(), m_chart->meta().offset);
            if (t > maxNoteMs)
                maxNoteMs = t;
        }
        duration = maxNoteMs + 1000.0;
    }

    if (duration <= 0.0)
        duration = 60000.0;
    m_durationMs = duration;
    if (m_currentTimeMs > m_durationMs)
        m_currentTimeMs = m_durationMs;
}

void DensityCurve::syncCurrentTime(double timeMs)
{
    if (m_durationMs <= 0.0)
        syncDuration();
    const double clamped = qBound(0.0, timeMs, m_durationMs);
    if (qAbs(clamped - m_currentTimeMs) < 0.05)
        return;
    m_currentTimeMs = clamped;
    update();
}

void DensityCurve::refreshFromChart()
{
    m_chart = (m_chartController ? m_chartController->chart() : nullptr);
    syncDuration();
    computeDensity();
    update();
}

void DensityCurve::updateFromCanvasBeat(double beat)
{
    if (!m_chart)
        return;
    int beatNum = 0;
    int numerator = 0;
    int denominator = 1;
    MathUtils::floatToBeat(beat, beatNum, numerator, denominator);
    const double timeMs = MathUtils::beatToMs(beatNum, numerator, denominator, m_chart->bpmList(), m_chart->meta().offset);
    syncCurrentTime(timeMs);
}

void DensityCurve::computeDensity()
{
    m_densityData.fill(0, kBinCount);
    if (!m_chart || m_durationMs <= 0.0)
        return;

    const auto &notes = m_chart->notes();
    for (const Note &note : notes)
    {
        if (note.type != NoteType::NORMAL && note.type != NoteType::RAIN)
            continue;

        const double t = MathUtils::beatToMs(
            note.beatNum, note.numerator, note.denominator, m_chart->bpmList(), m_chart->meta().offset);
        const double clamped = qBound(0.0, t, m_durationMs);
        int bin = static_cast<int>(qFloor((clamped / m_durationMs) * kBinCount));
        if (bin >= kBinCount)
            bin = kBinCount - 1;
        if (bin < 0)
            bin = 0;
        m_densityData[bin] += 1;
    }
}

void DensityCurve::updateFromPointer(const QPoint &pos, bool commitSeek)
{
    if (height() <= 0 || m_durationMs <= 0.0)
        return;
    const int y = qBound(0, pos.y(), height());
    const double ratio = 1.0 - (static_cast<double>(y) / static_cast<double>(height()));
    const double timeMs = qBound(0.0, ratio * m_durationMs, m_durationMs);
    m_tipTimeMs = timeMs;
    m_currentTimeMs = timeMs;
    if (commitSeek)
        emit seekRequested(timeMs);
    else
        emit seekPreviewRequested(timeMs);
    update();
}

void DensityCurve::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void DensityCurve::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!m_dragging && m_showTip)
    {
        m_showTip = false;
        update();
    }
}

void DensityCurve::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    const QColor baseBg = Settings::instance().backgroundColor();
    const double luminance = 0.2126 * baseBg.redF() + 0.7152 * baseBg.greenF() + 0.0722 * baseBg.blueF();
    const bool darkTheme = (luminance < 0.5);
    const QColor curveBg = darkTheme ? baseBg.lighter(118) : baseBg.darker(106);
    const QColor barColor = darkTheme ? QColor(235, 238, 245, 220) : QColor(35, 35, 35, 210);
    const QColor playheadColor = darkTheme ? QColor(255, 112, 87) : QColor(219, 68, 55);
    const QColor borderColor = darkTheme ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 50);

    painter.fillRect(rect(), curveBg);
    painter.setPen(borderColor);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    int maxCount = 0;
    for (int val : m_densityData)
    {
        if (val > maxCount)
            maxCount = val;
    }
    maxCount = qMax(maxCount, kRefMaxCount);

    const int fullWidth = qMax(1, width() - 4);
    const int h = qMax(1, height());
    for (int i = 0; i < kBinCount; ++i)
    {
        const int rowIndex = (kBinCount - 1 - i);
        const int y0 = (rowIndex * h) / kBinCount;
        const int y1 = ((rowIndex + 1) * h) / kBinCount;
        const int rowH = qMax(1, y1 - y0);
        const int gap = (rowH >= 3) ? 1 : 0; // leave a subtle seam between bins
        const int drawY = y0 + gap;
        const int drawH = qMax(1, rowH - gap);
        const int barW = (m_densityData[i] * fullWidth) / qMax(1, maxCount);
        if (barW <= 0)
            continue;
        painter.fillRect(width() - 2 - barW, drawY, barW, drawH, barColor);
    }

    const double clampedTime = qBound(0.0, m_currentTimeMs, m_durationMs);
    const int playY = static_cast<int>((1.0 - (clampedTime / qMax(1.0, m_durationMs))) * h);
    painter.setPen(QPen(playheadColor, 2));
    painter.drawLine(0, playY, width(), playY);

    if (m_showTip)
    {
        const QString text = formatTimeMs(m_tipTimeMs);
        const QFontMetrics fm(font());
        const int padX = 8;
        const int padY = 4;
        const int tipW = fm.horizontalAdvance(text) + padX * 2;
        const int tipH = fm.height() + padY * 2;
        int tipY = playY - tipH / 2;
        tipY = qBound(2, tipY, qMax(2, height() - tipH - 2));
        QRect tipRect(-tipW - 8, tipY, tipW, tipH);
        const QColor tipBg = darkTheme ? QColor(16, 18, 24, 220) : QColor(245, 246, 248, 232);
        const QColor tipFg = darkTheme ? QColor(242, 244, 248) : QColor(34, 34, 34);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tipBg);
        painter.drawRoundedRect(tipRect, 6, 6);
        painter.setPen(tipFg);
        painter.drawText(tipRect, Qt::AlignCenter, text);
    }
}

void DensityCurve::mousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_showTip = true;
    emit seekGestureStarted();
    updateFromPointer(event->pos(), false);
    event->accept();
}

void DensityCurve::mouseMoveEvent(QMouseEvent *event)
{
    if (!event)
        return;
    if (!m_dragging || !(event->buttons() & Qt::LeftButton))
        return;
    updateFromPointer(event->pos(), false);
    event->accept();
}

void DensityCurve::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
        return;
    if (m_dragging)
    {
        updateFromPointer(event->pos(), true);
        m_dragging = false;
        m_showTip = false;
        emit seekGestureFinished();
        update();
    }
    event->accept();
}

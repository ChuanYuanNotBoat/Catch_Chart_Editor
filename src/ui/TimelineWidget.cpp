#include "TimelineWidget.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "utils/MathUtils.h"
#include <QPainter>
#include <QMouseEvent>
#include <QStyleOption>

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent), m_chartController(nullptr), m_playbackController(nullptr), m_currentTime(0), m_totalDuration(0), m_offset(0), m_dragging(false)
{
    setFixedHeight(30);
}

void TimelineWidget::setChartController(ChartController *controller)
{
    m_chartController = controller;
    m_offset = 0;
    // 计算总时长
    if (controller && controller->chart())
    {
        m_offset = controller->chart()->meta().offset;
        if (!controller->chart()->notes().isEmpty())
        {
            double maxChartTime = 0; // 最大谱面时间（不含offset）
            for (const Note &note : controller->chart()->notes())
            {
                double t = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator,
                                               controller->chart()->bpmList(), 0); // 不使用offset
                if (t > maxChartTime)
                    maxChartTime = t;
            }
            m_totalDuration = maxChartTime + m_offset + 1000; // 总时长 = 谱面时间 + offset + 缓冲
        }
        else
        {
            m_totalDuration = 60000 + m_offset; // 默认1分钟加上offset
        }
    }
    else
    {
        m_totalDuration = 60000; // 默认1分钟
    }
    update();
}

void TimelineWidget::setPlaybackController(PlaybackController *controller)
{
    if (m_playbackController)
    {
        disconnect(m_playbackController, &PlaybackController::positionChanged, this, &TimelineWidget::updateFromPlayback);
        disconnect(this, &TimelineWidget::seekRequested, m_playbackController, &PlaybackController::seekTo);
    }

    m_playbackController = controller;
    if (!m_playbackController)
        return;

    connect(m_playbackController, &PlaybackController::positionChanged, this, &TimelineWidget::updateFromPlayback);
    connect(this, &TimelineWidget::seekRequested, m_playbackController, &PlaybackController::seekTo);
}

void TimelineWidget::updateFromPlayback(double timeMs)
{
    if (m_dragging)
        return;
    // 音频位置转谱面时间：减去offset
    m_currentTime = timeMs - m_offset;
    if (m_currentTime < 0)
        m_currentTime = 0;
    update();
}

void TimelineWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(40, 40, 40));
    // The background is a fixed dark color: use high-contrast tick/label
    // colors so they never blend into it.
    const QColor tickColor(150, 150, 150);
    const QColor labelColor(220, 220, 220);
    if (m_totalDuration <= 0)
        return;
    // 画时间标尺
    int totalPixels = width();
    double pixelPerMs = totalPixels / m_totalDuration;
    // 画刻度线（每秒）
    for (int sec = 0; sec <= static_cast<int>(m_totalDuration / 1000); ++sec)
    {
        int x = static_cast<int>(sec * 1000 * pixelPerMs);
        painter.setPen(tickColor);
        painter.drawLine(x, 0, x, height());
        painter.setPen(labelColor);
        painter.drawText(x + 2, 12, QString::number(sec));
    }
    // 画播放头（需要转换为音频位置）
    double audioPos = m_currentTime + m_offset;
    int headX = static_cast<int>(audioPos * pixelPerMs);
    painter.setPen(Qt::red);
    painter.drawLine(headX, 0, headX, height());
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton || !m_playbackController)
        return;
    m_dragging = true;
    emit seekGestureStarted();
    updateFromPointer(event->pos().x(), false);
    event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!event || !m_dragging || !(event->buttons() & Qt::LeftButton) || !m_playbackController)
        return;
    updateFromPointer(event->pos().x(), false);
    event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton)
        return;
    if (m_dragging)
    {
        updateFromPointer(event->pos().x(), true);
        m_dragging = false;
        emit seekGestureFinished();
    }
    event->accept();
}

double TimelineWidget::pointerToAudioPositionMs(int x) const
{
    if (width() <= 0 || m_totalDuration <= 0.0)
        return 0.0;
    const double ratio = qBound(0.0, static_cast<double>(x) / static_cast<double>(width()), 1.0);
    const double chartTime = ratio * qMax(0.0, m_totalDuration - m_offset);
    const double audioPos = chartTime + m_offset;
    return qBound(0.0, audioPos, m_totalDuration);
}

void TimelineWidget::updateFromPointer(int x, bool commitSeek)
{
    const double audioPos = pointerToAudioPositionMs(x);
    m_currentTime = qMax(0.0, audioPos - m_offset);
    if (commitSeek)
        emit seekRequested(audioPos);
    else
        emit seekPreviewRequested(audioPos);
    update();
}

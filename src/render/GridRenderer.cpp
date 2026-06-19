#include "GridRenderer.h"
#include "render/BeatDivisionColor.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include <QDebug>
#include <QPainter>
#include <numeric>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace
{
int reducedDenominator(int numerator, int denominator)
{
    if (denominator <= 0)
        return 1;
    if (numerator == 0)
        return 1;

    const int n = qAbs(numerator);
    const int g = std::gcd(n, denominator);
    if (g <= 0)
        return denominator;
    return denominator / g;
}

bool shouldColorizeDivision(int reducedDen)
{
    return reducedDen == 2 || reducedDen == 3 || reducedDen == 4 || reducedDen == 6;
}

bool shouldColorizeByPolicy(int reducedDen, int timeDivision,
                            const QString &colorPreset, const QSet<int> &customDivisions)
{
    const QString preset = colorPreset.trimmed().toLower();
    if (preset == "all")
        return reducedDen > 0;
    if (preset == "classic")
    {
        if (timeDivision >= 8)
            return reducedDen == 2 || reducedDen == 4;
        return shouldColorizeDivision(reducedDen);
    }
    return customDivisions.contains(reducedDen);
}
}

void GridRenderer::drawGridBeatLinear(QPainter &painter, const QRect &rect, int xDivisions,
                                      double startBeat, double endBeat, double timeDivision,
                                      const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                                      bool verticalFlip,
                                      bool colorizeTimeDivisions,
                                      const QString &colorPreset,
                                      const QList<int> &customDivisions,
                                      const QVector<QPair<double, double>> *excludedRanges)
{
    try
    {
        // 垂直分隔线（与 drawGrid 相同）
        const double stepX = static_cast<double>(rect.width()) / xDivisions;
        painter.setPen(QPen(Qt::lightGray, 1, Qt::DotLine));
        for (int i = 1; i < xDivisions; ++i)
        {
            const int x = rect.left() + static_cast<int>(std::round(i * stepX));
            painter.drawLine(x, rect.top(), x, rect.bottom());
        }

        if (bpmCache.isEmpty())
            return;

        const double totalBeatRange = endBeat - startBeat;
        if (totalBeatRange <= 0)
            return;

        int timeDivInt = static_cast<int>(timeDivision);
        if (timeDivInt <= 0)
            timeDivInt = 1;

        // 在 beat 线性模式下，tick 均匀分布在 Y 轴上
        // Y = rect.top() + (beat - startBeat) / totalBeatRange * rect.height()
        const int startTick = static_cast<int>(std::ceil(startBeat * timeDivInt));
        const int endTick = static_cast<int>(std::floor(endBeat * timeDivInt));

        const double pixelsPerBeat = rect.height() / totalBeatRange;
        const double minPixelStep = 5.0;
        const double beatPerTick = 1.0 / timeDivInt;
        const bool skipDenseTicks = (beatPerTick * pixelsPerBeat < minPixelStep);

        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);

        int lastDrawnY = -9999;
        const QSet<int> customSet = QSet<int>(customDivisions.begin(), customDivisions.end());
        for (int tick = startTick; tick <= endTick; ++tick)
        {
            const int beatNum = tick / timeDivInt;
            const int numerator = tick % timeDivInt;
            const int denominator = timeDivInt;
            const bool isIntegerBeat = (numerator == 0);

            const double beat = beatNum + static_cast<double>(numerator) / denominator;
            if (beat < startBeat || beat > endBeat)
                continue;

            // Beat 线性映射到 Y
            int y = 0;
            if (!verticalFlip)
                y = rect.top() + static_cast<int>((beat - startBeat) / totalBeatRange * rect.height());
            else
                y = rect.bottom() - static_cast<int>((beat - startBeat) / totalBeatRange * rect.height());

            if (skipDenseTicks && !isIntegerBeat)
                continue;

            if (qAbs(y - lastDrawnY) < 1)
                continue;
            lastDrawnY = y;

            QPen linePen(Qt::gray, isIntegerBeat ? 2 : 1);
            if (colorizeTimeDivisions)
            {
                const int reducedDen = reducedDenominator(numerator, denominator);
                if (shouldColorizeByPolicy(reducedDen, timeDivInt, colorPreset, customSet))
                    linePen.setColor(BeatDivisionColor::noteColorForDivision(reducedDen, numerator));
            }

            painter.setPen(linePen);
            painter.drawLine(rect.left(), y, rect.right(), y);

            if (isIntegerBeat)
            {
                // 跳过排除范围内的全局编号（由 drawExcludedRangeGrid 绘制独立编号）
                bool inExcluded = false;
                if (excludedRanges)
                {
                    for (const auto &range : *excludedRanges)
                    {
                        if (beat >= range.first && beat < range.second)
                        {
                            inExcluded = true;
                            break;
                        }
                    }
                }
                if (!inExcluded)
                {
                    const QString text = QString::number(beatNum);
                    painter.setPen(Qt::darkGray);
                    int textY = y;
                    if (verticalFlip)
                    {
                        textY = y + 12;
                        if (textY > rect.bottom())
                            textY = y - 12;
                    }
                    else
                    {
                        textY = y - 2;
                        if (textY < rect.top())
                            textY = y + 12;
                    }
                    painter.drawText(rect.left() + 2, textY, text);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("GridRenderer::drawGridBeatLinear - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("GridRenderer::drawGridBeatLinear - Unknown exception");
    }
}

void GridRenderer::drawExcludedRangeGrid(QPainter &painter, const QRect &canvasRect, int xDivisions,
                                         double rangeStartBeat, double rangeEndBeat,
                                         double visibleStartBeat, double visibleEndBeat,
                                         double visibleStartY, double visibleEndY,
                                         bool useTimeLinear,
                                         double timeDivision,
                                         double scrollTimeMs, double pixelsPerMs,
                                         const QVector<MathUtils::BpmCacheEntry> *bpmCache,
                                         bool verticalFlip)
{
    try
    {
        if (rangeEndBeat <= rangeStartBeat)
            return;
        if (!bpmCache || bpmCache->isEmpty())
            return;
        if (useTimeLinear && pixelsPerMs <= 0.0)
            return;

        // 裁剪边界（橙色矩形区域）
        const int effectiveClipTop = static_cast<int>(std::floor(std::min(visibleStartY, visibleEndY)));
        const int effectiveClipBottom = static_cast<int>(std::ceil(std::max(visibleStartY, visibleEndY)));
        if (effectiveClipBottom <= effectiveClipTop)
            return;

        // 垂直分隔线（裁剪到橙色矩形范围内）
        const double stepX = static_cast<double>(canvasRect.width()) / xDivisions;
        painter.setPen(QPen(QColor(255, 165, 0, 100), 1, Qt::DotLine));
        for (int i = 1; i < xDivisions; ++i)
        {
            const int x = canvasRect.left() + static_cast<int>(std::round(i * stepX));
            painter.drawLine(x, effectiveClipTop, x, effectiveClipBottom);
        }

        int timeDivInt = static_cast<int>(timeDivision);
        if (timeDivInt <= 0)
            timeDivInt = 1;

        // 使用 floor/ceil 扩展 tick 范围
        const int startTick = static_cast<int>(std::floor(rangeStartBeat * timeDivInt));
        const int endTick = static_cast<int>(std::ceil(rangeEndBeat * timeDivInt));

        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);

        // 独立编号：以 rangeStartBeat 为基准，第一个整拍编号为 1，固定不随滚动变化
        const int firstIndependentBeat = static_cast<int>(std::ceil(rangeStartBeat - 1e-9));
        int lastDrawnY = -9999;

        for (int tick = startTick; tick <= endTick; ++tick)
        {
            const int beatNum = tick / timeDivInt;
            const int numerator = tick % timeDivInt;
            const bool isIntegerBeat = (numerator == 0);

            const double beat = beatNum + static_cast<double>(numerator) / timeDivInt;
            if (beat < rangeStartBeat || beat > rangeEndBeat)
                continue;

            // 使用过滤缓存进行 time-based Y 映射（网格高度仅由真实BPM决定）
            int y = 0;
            if (useTimeLinear)
            {
                const double tickMs = MathUtils::beatToMs(beat, *bpmCache);
                const int yPx = static_cast<int>((tickMs - scrollTimeMs) * pixelsPerMs);
                if (!verticalFlip)
                    y = canvasRect.top() + yPx;
                else
                    y = canvasRect.bottom() - yPx;
            }
            else
            {
                const double denom = visibleEndBeat - visibleStartBeat;
                if (std::abs(denom) <= 1e-9)
                    continue;
                const double t = (beat - visibleStartBeat) / denom;
                y = static_cast<int>(std::round(visibleStartY + t * (visibleEndY - visibleStartY)));
            }

            // 跳过超出橙色矩形范围的线
            if (y < effectiveClipTop - 1 || y > effectiveClipBottom + 1)
                continue;

            if (qAbs(y - lastDrawnY) < 1)
                continue;
            lastDrawnY = y;

            // 网格线颜色：橙色半透明
            QPen linePen(QColor(255, 165, 0, isIntegerBeat ? 120 : 60), isIntegerBeat ? 2 : 1);
            painter.setPen(linePen);
            painter.drawLine(canvasRect.left(), y, canvasRect.right(), y);

            if (isIntegerBeat)
            {
                int textY = y;
                if (verticalFlip)
                {
                    textY = y + 12;
                    if (textY > effectiveClipBottom)
                        textY = y - 12;
                }
                else
                {
                    textY = y - 2;
                    if (textY < effectiveClipTop)
                        textY = y + 12;
                }

                // 排除项矩形内统一使用独立编号，橙色斜体
                const int specialNum = beatNum - firstIndependentBeat + 1;
                if (specialNum <= 0)
                    continue;
                font.setItalic(true);
                painter.setFont(font);
                painter.setPen(QColor(255, 140, 0));
                painter.drawText(canvasRect.left() + 2, textY, QString::number(specialNum));
            }
        }

        // Fallback：如果矩形内无可见网格线，绘制一条参考虚线
        if (lastDrawnY == -9999)
        {
            int midY = (effectiveClipTop + effectiveClipBottom) / 2;
            painter.setPen(QPen(QColor(255, 165, 0, 80), 1, Qt::DashLine));
            painter.drawLine(canvasRect.left(), midY, canvasRect.right(), midY);
            painter.setPen(QColor(255, 140, 0));
            font.setItalic(true);
            painter.setFont(font);
            painter.drawText(canvasRect.left() + 2, midY - 2, "[excluded]");
        }

        // 恢复字体
        font.setItalic(false);
        painter.setFont(font);
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("GridRenderer::drawExcludedRangeGrid - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("GridRenderer::drawExcludedRangeGrid - Unknown exception");
    }
}

void GridRenderer::drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                            double startTime, double endTime, double timeDivision,
                            const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                            bool verticalFlip,
                            bool colorizeTimeDivisions,
                            const QString &colorPreset,
                            const QList<int> &customDivisions,
                            const QVector<MathUtils::BpmCacheEntry> *fullBpmCache,
                            const QVector<QPair<double, double>> *excludedRanges)
{
    try
    {
        const double stepX = static_cast<double>(rect.width()) / xDivisions;
        painter.setPen(QPen(Qt::lightGray, 1, Qt::DotLine));
        for (int i = 1; i < xDivisions; ++i)
        {
            const int x = rect.left() + static_cast<int>(std::round(i * stepX));
            painter.drawLine(x, rect.top(), x, rect.bottom());
        }

        if (bpmCache.isEmpty())
            return;

        const double totalDuration = endTime - startTime;
        if (totalDuration <= 0)
            return;

        // 使用过滤缓存进行时间-beat转换（统一使用 MathUtils）
        const double startBeatPos = MathUtils::msToBeatFloat(startTime, bpmCache);
        const double endBeatPos = MathUtils::msToBeatFloat(endTime, bpmCache);

        // 预计算排除范围的 ms 区间（使用过滤缓存 bpmCache，
        // 与主循环中 tickMs 的 beat→ms 转换使用同一缓存，
        // 确保排除范围跳过边界一致。）
        QVector<QPair<double, double>> excludedRangesMs;
        if (excludedRanges && fullBpmCache && !fullBpmCache->isEmpty())
        {
            for (const auto &range : *excludedRanges)
            {
                double ms1 = MathUtils::beatToMs(range.first, bpmCache);
                double ms2 = MathUtils::beatToMs(range.second, bpmCache);
                excludedRangesMs.append(qMakePair(ms1, ms2));
            }
        }

        auto isInExcludedRangeByTime = [&](double tickMsValue) -> bool
        {
            for (const auto &range : excludedRangesMs)
            {
                if (tickMsValue >= range.first && tickMsValue <= range.second)
                    return true;
            }
            return false;
        };

        int timeDivInt = static_cast<int>(timeDivision);
        if (timeDivInt <= 0)
            timeDivInt = 1;

        const double totalBeatRange = endBeatPos - startBeatPos;
        if (totalBeatRange <= 0)
            return;

        // TimeLinear: 使用固定的 pixelsPerMs = rect.height() / totalDuration
        const double pixelsPerMs = (totalDuration > 0) ? (rect.height() / totalDuration) : 1.0;

        const int startTick = static_cast<int>(std::ceil(startBeatPos * timeDivInt));
        const int endTick = static_cast<int>(std::floor(endBeatPos * timeDivInt));

        // TimeLinear模式：不跳过细线，网格线密度由用户设置的timeDivision决定
        const bool skipDenseTicks = false;

        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);

        int lastDrawnY = -9999;
        const QSet<int> customSet = QSet<int>(customDivisions.begin(), customDivisions.end());
        for (int tick = startTick; tick <= endTick; ++tick)
        {
            const int beatNum = tick / timeDivInt;
            const int numerator = tick % timeDivInt;
            const int denominator = timeDivInt;
            const bool isIntegerBeat = (numerator == 0);

            const double beat = beatNum + static_cast<double>(numerator) / denominator;
            if (beat < startBeatPos || beat > endBeatPos)
                continue;

            // TimeLinear: beat → ms → Y，基于固定 pixelsPerMs（使用过滤缓存）
            const double tickMs = MathUtils::beatToMs(beat, bpmCache);

            // 跳过排除范围内的tick（使用时间比较避免过滤缓存beat空间不匹配）
            if (isInExcludedRangeByTime(tickMs))
                continue;
            const int yPx = static_cast<int>((tickMs - startTime) * pixelsPerMs);

            int y = 0;
            if (!verticalFlip)
                y = rect.top() + yPx;
            else
                y = rect.bottom() - yPx;

            if (skipDenseTicks && !isIntegerBeat)
                continue;

            if (qAbs(y - lastDrawnY) < 1)
                continue;
            lastDrawnY = y;

            QPen linePen(Qt::gray, isIntegerBeat ? 2 : 1);
            if (colorizeTimeDivisions)
            {
                const int reducedDen = reducedDenominator(numerator, denominator);
                if (shouldColorizeByPolicy(reducedDen, timeDivInt, colorPreset, customSet))
                    linePen.setColor(BeatDivisionColor::noteColorForDivision(reducedDen, numerator));
            }

            painter.setPen(linePen);
            painter.drawLine(rect.left(), y, rect.right(), y);

            if (isIntegerBeat)
            {
                // 当提供完整BPM缓存时，用tickMs反查实际beat编号
                // （排除项过滤后的beat空间中beatNum不准确）
                int displayBeatNum = beatNum;
                if (fullBpmCache && !fullBpmCache->isEmpty())
                {
                    double actualBeat = MathUtils::msToBeatFloat(tickMs, *fullBpmCache);
                    displayBeatNum = static_cast<int>(std::floor(actualBeat + 0.5));
                }
                const QString text = QString::number(displayBeatNum);
                painter.setPen(Qt::darkGray);
                int textY = y;
                if (verticalFlip)
                {
                    textY = y + 12;
                    if (textY > rect.bottom())
                        textY = y - 12;
                }
                else
                {
                    textY = y - 2;
                    if (textY < rect.top())
                        textY = y + 12;
                }
                painter.drawText(rect.left() + 2, textY, text);
            }
        }
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("GridRenderer::drawGrid - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("GridRenderer::drawGrid - Unknown exception");
    }
}
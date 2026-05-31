#include "GridRenderer.h"
#include "render/BeatDivisionColor.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include <QDebug>
#include <QPainter>
#include <numeric>
#include <QSet>

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
                                      const QList<int> &customDivisions)
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
    catch (const std::exception &e)
    {
        Logger::error(QString("GridRenderer::drawGridBeatLinear - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("GridRenderer::drawGridBeatLinear - Unknown exception");
    }
}

void GridRenderer::drawExcludedRangeGrid(QPainter &painter, const QRect &rect, int xDivisions,
                                          double rangeStartBeat, double rangeEndBeat,
                                          double timeDivision, double bpm,
                                          bool verticalFlip)
{
    try
    {
        if (rangeEndBeat <= rangeStartBeat)
            return;
        if (bpm <= 0.0)
            return;

        // 垂直分隔线
        const double stepX = static_cast<double>(rect.width()) / xDivisions;
        painter.setPen(QPen(QColor(255, 165, 0, 100), 1, Qt::DotLine));
        for (int i = 1; i < xDivisions; ++i)
        {
            const int x = rect.left() + static_cast<int>(std::round(i * stepX));
            painter.drawLine(x, rect.top(), x, rect.bottom());
        }

        int timeDivInt = static_cast<int>(timeDivision);
        if (timeDivInt <= 0)
            timeDivInt = 1;

        const double totalBeatRange = rangeEndBeat - rangeStartBeat;
        const int startTick = static_cast<int>(std::ceil(rangeStartBeat * timeDivInt));
        const int endTick = static_cast<int>(std::floor(rangeEndBeat * timeDivInt));

        QFont font = painter.font();
        font.setPointSize(8);
        painter.setFont(font);

        // 开头和结尾的全局拍数（取整）
        const int globalStartBeat = static_cast<int>(std::ceil(rangeStartBeat));
        const int globalEndBeat = static_cast<int>(std::floor(rangeEndBeat));

        // 特殊编号计数器：从第一个内部beat开始从1递增
        int specialNum = 1;
        int lastDrawnY = -9999;

        for (int tick = startTick; tick <= endTick; ++tick)
        {
            const int beatNum = tick / timeDivInt;
            const int numerator = tick % timeDivInt;
            const bool isIntegerBeat = (numerator == 0);

            const double beat = beatNum + static_cast<double>(numerator) / timeDivInt;
            if (beat < rangeStartBeat || beat > rangeEndBeat)
                continue;

            // Beat 线性映射到 Y
            int y = 0;
            if (!verticalFlip)
                y = rect.top() + static_cast<int>((beat - rangeStartBeat) / totalBeatRange * rect.height());
            else
                y = rect.bottom() - static_cast<int>((beat - rangeStartBeat) / totalBeatRange * rect.height());

            if (qAbs(y - lastDrawnY) < 1)
                continue;
            lastDrawnY = y;

            // 网格线颜色：橙色半透明
            QPen linePen(QColor(255, 165, 0, isIntegerBeat ? 120 : 60), isIntegerBeat ? 2 : 1);
            painter.setPen(linePen);
            painter.drawLine(rect.left(), y, rect.right(), y);

            if (isIntegerBeat)
            {
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

                // 开头和结尾：全局拍数，正常样式
                if (beatNum == globalStartBeat || beatNum == globalEndBeat)
                {
                    font.setItalic(false);
                    painter.setFont(font);
                    painter.setPen(Qt::darkGray);
                    painter.drawText(rect.left() + 2, textY, QString::number(beatNum));
                }
                else
                {
                    // 内部：特殊编号，橙色斜体
                    font.setItalic(true);
                    painter.setFont(font);
                    painter.setPen(QColor(255, 140, 0));
                    painter.drawText(rect.left() + 2, textY, QString::number(specialNum));
                    specialNum++;
                }
            }
        }

        // 恢复字体
        font.setItalic(false);
        painter.setFont(font);
    }
    catch (const std::exception &e)
    {
        Q_UNUSED(e);
    }
    catch (...)
    {
    }
}

void GridRenderer::drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                            double startTime, double endTime, double timeDivision,
                            const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                            bool verticalFlip,
                            bool colorizeTimeDivisions,
                            const QString &colorPreset,
                            const QList<int> &customDivisions)
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

        auto findBeatFromTime = [&](double timeMs) -> double
        {
            if (bpmCache.isEmpty())
                return 0.0;
            int lo = 0, hi = bpmCache.size() - 1;
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                if (bpmCache[mid].accumulatedMs <= timeMs)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            const auto &seg = bpmCache[lo];
            const double beatOffset = (timeMs - seg.accumulatedMs) * (seg.bpm / 60000.0);
            return seg.beatPos + beatOffset;
        };

        // beat → ms，用于 time-based Y 映射
        auto beatToMs = [&](double beatPos) -> double
        {
            if (bpmCache.isEmpty())
                return 0.0;
            int lo = 0, hi = bpmCache.size() - 1;
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                if (bpmCache[mid].beatPos <= beatPos)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            const auto &seg = bpmCache[lo];
            const double beatOffset = beatPos - seg.beatPos;
            const double msPerBeat = (seg.bpm > 0.0) ? 60000.0 / seg.bpm : 1000.0;
            return seg.accumulatedMs + beatOffset * msPerBeat;
        };

        const double startBeatPos = findBeatFromTime(startTime);
        const double endBeatPos = findBeatFromTime(endTime);

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

            // TimeLinear: beat → ms → Y，基于固定 pixelsPerMs
            const double tickMs = beatToMs(beat);
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
    catch (const std::exception &e)
    {
        Logger::error(QString("GridRenderer::drawGrid - Exception: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("GridRenderer::drawGrid - Unknown exception");
    }
}

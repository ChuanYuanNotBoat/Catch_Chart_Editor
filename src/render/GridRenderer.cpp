#include "GridRenderer.h"
#include "render/BeatDivisionColor.h"
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

void GridRenderer::drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                            double startBeat, double endBeat, double timeDivision,
                            bool verticalFlip,
                            bool colorizeTimeDivisions,
                            const QString &colorPreset,
                            const QList<int> &customDivisions,
                            int beatNumberFontSize,
                            int beatNumberLeftMargin)
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

        const double totalBeats = endBeat - startBeat;
        if (totalBeats <= 0)
            return;

        int timeDivInt = static_cast<int>(timeDivision);
        if (timeDivInt <= 0)
            timeDivInt = 1;

        const int startTick = static_cast<int>(std::ceil(startBeat * timeDivInt));
        const int endTick = static_cast<int>(std::floor(endBeat * timeDivInt));

        const double pixelsPerBeat = rect.height() / totalBeats;
        const double minPixelStep = 5.0;
        const double beatsPerTick = 1.0 / timeDivInt;
        const bool skipDenseTicks = (beatsPerTick * pixelsPerBeat < minPixelStep);

        QFont font = painter.font();
        font.setPointSize(beatNumberFontSize);
        painter.setFont(font);
        QFontMetrics fm(font);

        int lastDrawnY = -9999;
        const QSet<int> customSet = QSet<int>(customDivisions.begin(), customDivisions.end());
        for (int tick = startTick; tick <= endTick; ++tick)
        {
            const int beatNum = tick / timeDivInt;
            const int numerator = tick % timeDivInt;
            const int denominator = timeDivInt;
            const bool isIntegerBeat = (numerator == 0);

            const double beatFloat = beatNum + static_cast<double>(numerator) / denominator;

            int y = 0;
            if (!verticalFlip)
                y = rect.top() + static_cast<int>((beatFloat - startBeat) / totalBeats * rect.height());
            else
                y = rect.bottom() - static_cast<int>((beatFloat - startBeat) / totalBeats * rect.height());

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

            if (isIntegerBeat)
            {
                const QString text = QString::number(beatNum);
                const int textWidth = fm.horizontalAdvance(text);
                const int padding = 2;

                // 小节编号右对齐到全宽线左边缘
                painter.setPen(Qt::darkGray);
                const int textX = rect.left() - textWidth - padding;
                const int textY = y + fm.ascent() / 2;
                painter.drawText(textX, textY, text);

                painter.setPen(linePen);
                painter.drawLine(rect.left(), y, rect.right(), y);
            }
            else
            {
                painter.setPen(linePen);
                painter.drawLine(rect.left(), y, rect.right(), y);
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
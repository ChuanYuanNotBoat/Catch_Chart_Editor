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
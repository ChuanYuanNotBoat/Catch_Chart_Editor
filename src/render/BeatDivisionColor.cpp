#include "BeatDivisionColor.h"

namespace BeatDivisionColor
{
    QColor noteColorForDivision(int denominator, int numerator)
    {
        Q_UNUSED(numerator);
        switch (denominator)
        {
        case 1:
            return QColor(255, 0, 0);
        case 2:
            return QColor(135, 206, 235);
        case 3:
            return QColor(0, 255, 0);
        case 4:
            return QColor(128, 0, 128);
        case 6:
            return QColor(0, 255, 0);
        case 8:
            return QColor(255, 215, 0);
        case 12:
            return QColor(0, 255, 0);
        case 16:
            return QColor(255, 215, 0);
        case 24:
            return QColor(0, 255, 0);
        case 32:
            return QColor(255, 215, 0);
        case 288:
            return QColor(0, 0, 255); // nobody will use it :O
        default:
            return QColor(0, 0, 255);
        }
    }
}

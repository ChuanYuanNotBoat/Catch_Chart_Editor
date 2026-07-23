#pragma once

#include <QPainter>
#include <QRect>
#include <QList>
#include <QString>

class GridRenderer
{
public:
    void drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                  double startBeat, double endBeat, double timeDivision,
                  bool verticalFlip = false,
                  bool colorizeTimeDivisions = false,
                  const QString &colorPreset = QString(),
                  const QList<int> &customDivisions = QList<int>(),
                  int beatNumberFontSize = 9,
                  int beatNumberLeftMargin = 0);
};

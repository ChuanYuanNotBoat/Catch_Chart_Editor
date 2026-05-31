#pragma once

#include <QPainter>
#include <QRect>
#include <QVector>
#include <QList>
#include <QString>
#include "utils/MathUtils.h"

class GridRenderer
{
public:
    // TimeLinear 模式：基于时间的网格绘制（现有行为）
    void drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                  double startTime, double endTime, double timeDivision,
                  const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                  bool verticalFlip = false,
                  bool colorizeTimeDivisions = false,
                  const QString &colorPreset = QString(),
                  const QList<int> &customDivisions = QList<int>());

    // BeatLinear 模式：基于拍号的网格绘制（修复 BPM 不一致问题）
    void drawGridBeatLinear(QPainter &painter, const QRect &rect, int xDivisions,
                            double startBeat, double endBeat, double timeDivision,
                            const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                            bool verticalFlip = false,
                            bool colorizeTimeDivisions = false,
                            const QString &colorPreset = QString(),
                            const QList<int> &customDivisions = QList<int>());

    // 排除范围内的网格绘制（带特殊编号：开头结尾显示全局拍数，内部从1开始递增）
    void drawExcludedRangeGrid(QPainter &painter, const QRect &rect, int xDivisions,
                               double rangeStartBeat, double rangeEndBeat,
                               double timeDivision, double bpm,
                               bool verticalFlip = false);
};

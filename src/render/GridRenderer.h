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
    // TimeLinear 模式：基于时间的网格绘制
    void drawGrid(QPainter &painter, const QRect &rect, int xDivisions,
                  double startTime, double endTime, double timeDivision,
                  const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                  bool verticalFlip = false,
                  bool colorizeTimeDivisions = false,
                  const QString &colorPreset = QString(),
                  const QList<int> &customDivisions = QList<int>(),
                  const QVector<MathUtils::BpmCacheEntry> *fullBpmCache = nullptr,
                  const QVector<QPair<double, double>> *excludedRanges = nullptr);

    // BeatLinear 模式：基于拍号的网格绘制（修复 BPM 不一致问题）
    void drawGridBeatLinear(QPainter &painter, const QRect &rect, int xDivisions,
                            double startBeat, double endBeat, double timeDivision,
                            const QVector<MathUtils::BpmCacheEntry> &bpmCache,
                            bool verticalFlip = false,
                            bool colorizeTimeDivisions = false,
                            const QString &colorPreset = QString(),
                            const QList<int> &customDivisions = QList<int>(),
                            const QVector<QPair<double, double>> *excludedRanges = nullptr);

    // 排除范围内的网格绘制（独立编号，使用time-based Y映射与主网格一致）
    // @param bpmCache  过滤后的BPM缓存（不含排除项BPM）。网格线高度仅由此缓存决定，保证每拍像素恒定。
    // 独立编号以 rangeStartBeat 为基准，第一个整拍编号为 1，固定不随滚动变化。
    void drawExcludedRangeGrid(QPainter &painter, const QRect &canvasRect, int xDivisions,
                               double rangeStartBeat, double rangeEndBeat,
                               double visibleStartBeat, double visibleEndBeat,
                               double visibleStartY, double visibleEndY,
                               bool useTimeLinear,
                               double timeDivision,
                               double scrollTimeMs, double pixelsPerMs,
                               const QVector<MathUtils::BpmCacheEntry> *bpmCache,
                               bool verticalFlip = false);
};

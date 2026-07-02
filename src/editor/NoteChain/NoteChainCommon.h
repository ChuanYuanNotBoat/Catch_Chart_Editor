#pragma once

// NoteChainCommon.h — 类型别名、常量定义、前向声明

#include <QPointF>
#include <QString>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QPair>

namespace NoteChain {

// ---- 链接形态 ----
constexpr const char *kShapeCurve    = "curve";
constexpr const char *kShapePolyline = "polyline";

// ---- 默认段密度分母 ----
constexpr int kDefaultSegmentDenominator = 4;

// ---- 渲染常量 ----
constexpr double kAnchorRadius       = 6.0;
constexpr double kHandleRadius       = 4.0;
constexpr double kHitTolerance       = 10.0;
constexpr double kDefaultHandleLength = 50.0;

// ---- 历史栈最大深度 ----
constexpr int kMaxHistoryDepth = 128;

// ---- 链接键类型（minId, maxId） ----
using LinkKey = QPair<int, int>;

// ---- 段形态映射 ----
using SegmentShapeMap      = QMap<LinkKey, QString>;
using SegmentDenominatorMap = QMap<LinkKey, int>;

} // namespace NoteChain
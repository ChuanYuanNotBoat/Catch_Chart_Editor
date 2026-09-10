#pragma once

#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace RainVisibilityIndex
{
inline QVector<double> buildPrefixMaxEndBeats(const QVector<int> &indicesByStartBeat,
                                               const QVector<double> &endBeats)
{
    QVector<double> prefix;
    prefix.reserve(indicesByStartBeat.size());
    double maximum = -std::numeric_limits<double>::infinity();
    for (int index : indicesByStartBeat)
    {
        if (index >= 0 && index < endBeats.size())
            maximum = std::max(maximum, endBeats[index]);
        prefix.append(maximum);
    }
    return prefix;
}

// firstStartingAtOrAfter is the lower-bound position in the start-sorted rain
// list. The returned position includes every earlier interval whose end can
// overlap startBeat, even when end beats are not monotonic.
inline qsizetype firstPotentiallyVisible(const QVector<double> &prefixMaxEndBeats,
                                         qsizetype firstStartingAtOrAfter,
                                         double startBeat)
{
    const qsizetype boundary = qBound<qsizetype>(0,
                                                 firstStartingAtOrAfter,
                                                 prefixMaxEndBeats.size());
    const auto begin = prefixMaxEndBeats.cbegin();
    const auto earlierEnd = begin + boundary;
    const auto firstOverlap = std::upper_bound(begin, earlierEnd, startBeat);
    return firstOverlap == earlierEnd ? boundary : std::distance(begin, firstOverlap);
}
} // namespace RainVisibilityIndex
